/// @file voxel_scene_patcher.cpp
/// @author dario
/// @date 22/09/2026

#include "voxel_scene_patcher.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <tracy/Tracy.hpp>
#include <unordered_map>
#include <utility>

namespace renderer {

namespace {

auto voxelPatchRecordDims(const voxel::gpu::VolumeRecord& record) -> glm::uvec3 {
	return {record.brick_dims_x, record.brick_dims_y, record.brick_dims_z};
}

auto voxelPatchGridWords(const voxel::gpu::VolumeRecord& record) -> uint64_t {
	return uint64_t {record.brick_dims_x} * record.brick_dims_y * record.brick_dims_z;
}

}

void VoxelScenePatcher::adopt(const voxel::gpu::PackedScene& scene, uint32_t pool_slots, std::vector<uint64_t> node_uids) {
	m_scene = scene;
	m_pool_slots = pool_slots;
	m_node_uids = std::move(node_uids);
	m_dead_grid_words = 0;
	m_valid = true;
}

auto VoxelScenePatcher::paletteMatches(const voxel::gpu::VolumeRecord& record, const voxel::Palette& palette) const -> bool {
	const size_t base = size_t {record.palette_offset} * 4u;
	return record.max_emissive == palette.max_emissive && base + voxel::gpu::k_palette_words <= m_scene.palettes.size() &&
	       std::memcmp(m_scene.palettes.data() + base, palette.entries.data(), voxel::gpu::k_palette_words * sizeof(uint32_t)) == 0;
}

auto VoxelScenePatcher::findOrAppendPalette(const voxel::Palette& palette) -> uint32_t {
	using voxel::gpu::k_palette_words;
	for (size_t base = 0; base + k_palette_words <= m_scene.palettes.size(); base += k_palette_words) {
		if (std::memcmp(m_scene.palettes.data() + base, palette.entries.data(), k_palette_words * sizeof(uint32_t)) == 0) {
			return static_cast<uint32_t>(base / 4u);
		}
	}
	const size_t base = m_scene.palettes.size();
	m_scene.palettes.resize(base + k_palette_words);
	std::memcpy(m_scene.palettes.data() + base, palette.entries.data(), k_palette_words * sizeof(uint32_t));
	return static_cast<uint32_t>(base / 4u);
}

auto VoxelScenePatcher::tryPatch(
    const voxel::BrickPool& pool, std::span<const voxel::gpu::SceneVolume> volumes, std::span<const uint64_t> node_uids,
    std::span<const VolumeDelta> deltas
) -> std::optional<Patched> {
	using namespace voxel::gpu;
	ZoneScoped;

	if (!m_valid || deltas.size() != volumes.size() || node_uids.size() != volumes.size()) {
		return std::nullopt;
	}

	std::unordered_map<uint64_t, uint32_t> live_record;
	for (uint32_t record = 0; record < m_node_uids.size(); ++record) {
		if (m_node_uids[record] != 0) {
			live_record.emplace(m_node_uids[record], record);
		}
	}

	// Decide before touching the retained copy so a bail out leaves it as the GPU holds it
	std::vector<std::optional<uint32_t>> kept_record(volumes.size());
	std::vector<bool> record_kept(m_node_uids.size(), false);
	uint64_t dirty_bricks = 0;
	uint64_t appended_grid_words = 0;
	for (size_t i = 0; i < volumes.size(); ++i) {
		const glm::uvec3 dims = volumes[i].volume->brickDims();
		const uint64_t bricks = uint64_t {dims.x} * dims.y * dims.z;
		const auto found = live_record.find(node_uids[i]);
		if (found != live_record.end()) {
			const VolumeRecord& record = m_scene.records[found->second];
			if (voxelPatchRecordDims(record) == dims && paletteMatches(record, *volumes[i].palette)) {
				kept_record[i] = found->second;
				record_kept[found->second] = true;
				dirty_bricks += deltas[i].unknown ? bricks : deltas[i].bricks.size();
				continue;
			}
		}
		dirty_bricks += bricks;
		appended_grid_words += bricks;
	}

	uint64_t dead_grid_words = m_dead_grid_words;
	for (uint32_t record = 0; record < m_node_uids.size(); ++record) {
		if (m_node_uids[record] != 0 && !record_kept[record]) {
			dead_grid_words += voxelPatchGridWords(m_scene.records[record]);
		}
	}

	// Dead records outweighing live ones are compacted by a full pack
	const uint64_t grid_words = m_scene.grids.size() + appended_grid_words;
	if (dead_grid_words * 2 > grid_words) {
		return std::nullopt;
	}

	// A dirty brick costs its slot and a grid word and a full pack costs the whole pool
	constexpr uint64_t k_words_per_dirty_brick = k_material_words_per_brick + k_occupancy_words_per_brick + 1;
	const uint64_t full_words = (uint64_t {m_pool_slots} * (k_material_words_per_brick + k_occupancy_words_per_brick)) + grid_words;
	if (dirty_bricks * k_words_per_dirty_brick * 2 > full_words) {
		return std::nullopt;
	}

	for (uint32_t record = 0; record < m_node_uids.size(); ++record) {
		if (!record_kept[record]) {
			m_node_uids[record] = 0;
		}
	}
	m_dead_grid_words = dead_grid_words;

	const auto first_new_palette_word = static_cast<uint32_t>(m_scene.palettes.size());
	std::vector<uint32_t> appended;
	std::vector<SceneVolume> by_record(m_scene.records.size());
	std::vector<VolumeDirty> dirty;
	std::vector<std::vector<uint32_t>> every_brick;
	every_brick.reserve(volumes.size());

	for (size_t i = 0; i < volumes.size(); ++i) {
		if (kept_record[i].has_value()) {
			by_record[*kept_record[i]] = volumes[i];
			if (deltas[i].unknown) {
				const glm::uvec3 dims = volumes[i].volume->brickDims();
				auto& all = every_brick.emplace_back(dims.x * dims.y * dims.z);
				std::iota(all.begin(), all.end(), 0u);
				dirty.push_back({.volume = *kept_record[i], .bricks = all});
			} else if (!deltas[i].bricks.empty()) {
				dirty.push_back({.volume = *kept_record[i], .bricks = deltas[i].bricks});
			}
			continue;
		}

		const glm::uvec3 dims = volumes[i].volume->brickDims();
		const glm::uvec3 coarse = coarseDims(dims);
		VolumeRecord record;
		record.brick_dims_x = dims.x;
		record.brick_dims_y = dims.y;
		record.brick_dims_z = dims.z;
		record.grid_offset = static_cast<uint32_t>(m_scene.grids.size());
		record.coarse_dims_x = coarse.x;
		record.coarse_dims_y = coarse.y;
		record.coarse_dims_z = coarse.z;
		record.coarse_offset = static_cast<uint32_t>(m_scene.coarse.size());
		record.palette_offset = findOrAppendPalette(*volumes[i].palette);
		record.max_emissive = volumes[i].palette->max_emissive;

		m_scene.grids.resize(m_scene.grids.size() + (dims.x * dims.y * dims.z), 0u);
		m_scene.coarse.resize(m_scene.coarse.size() + (((coarse.x * coarse.y * coarse.z) + 31u) / 32u), 0u);
		appended.push_back(static_cast<uint32_t>(m_scene.records.size()));
		m_scene.records.push_back(record);
		m_node_uids.push_back(node_uids[i]);
		by_record.push_back(volumes[i]);
	}

	Patched out;
	out.patch = computePatch(m_scene, m_pool_slots, pool, by_record, dirty, appended, first_new_palette_word);
	m_pool_slots = out.patch.pool_slots;
	out.record_uids = m_node_uids;
	out.record_dims.reserve(m_scene.records.size());
	for (const VolumeRecord& record : m_scene.records) {
		out.record_dims.push_back(voxelPatchRecordDims(record));
	}
	return out;
}

void VoxelScenePatcher::reset() {
	m_scene = {};
	m_pool_slots = 0;
	m_node_uids.clear();
	m_dead_grid_words = 0;
	m_valid = false;
}

}
