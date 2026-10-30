/// @file vulkan_renderer_voxel.cpp
/// @author dario
/// @date 16/05/2026

#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/debug/editor_overlays.hpp"
#include "toast/renderer/debug/voxel_debug.hpp"
#include "toast/renderer/lighting/clustered_lighting_constants.hpp"
#include "toast/renderer/lighting/cube_face_basis.hpp"
#include "toast/renderer/lighting/frustum.hpp"
#include "toast/renderer/lighting/light_culling.hpp"
#include "toast/renderer/lighting/shadow_slots.hpp"
#include "toast/renderer/passes/debug/selection_outline.hpp"
#include "toast/renderer/passes/geometry/debris_pass.hpp"
#include "toast/renderer/passes/geometry/depth_prepass.hpp"
#include "toast/renderer/passes/geometry/material_pass.hpp"
#include "toast/renderer/passes/geometry/skinning_pass.hpp"
#include "toast/renderer/passes/geometry/voxel_pass.hpp"
#include "toast/renderer/passes/lighting/environment_pass.hpp"
#include "toast/renderer/passes/lighting/reflection_probe_pass.hpp"
#include "toast/renderer/passes/lighting/shadow_pass.hpp"
#include "toast/renderer/passes/lighting/traced_specular_pass.hpp"
#include "toast/renderer/passes/post/taa_pass.hpp"
#include "toast/renderer/raytracing/ray_tracing_scene.hpp"
#include "toast/renderer/raytracing/skinned_blas_pool.hpp"
#include "toast/renderer/resources/blue_noise.hpp"
#include "toast/renderer/settings/renderer_settings.hpp"
#include "toast/renderer/shaders/shader_compiler.hpp"
#include "toast/renderer/vulkan_renderer.hpp"
#include "toast/renderer/vulkan_renderer_detail.hpp"

#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <toast/assets/animation.hpp>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/assets/material.hpp>
#include <toast/log.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/profiler_phases.hpp>
#include <toast/settings/settings.hpp>
#include <toast/thread_pool.hpp>
#include <toast/time.hpp>
#include <toast/voxel/nodes/voxel_node.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <toast/voxel/volume_bounds.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <toast/window/window_events.hpp>
#include <toast/world/animation_player.hpp>
#include <toast/world/camera.hpp>
#include <toast/world/directional_light.hpp>
#include <toast/world/fog_volume.hpp>
#include <toast/world/irradiance_volume.hpp>
#include <toast/world/light.hpp>
#include <toast/world/light_gizmo.hpp>
#include <toast/world/mesh_node.hpp>
#include <toast/world/point_light.hpp>
#include <toast/world/post_process_volume.hpp>
#include <toast/world/reflection_probe.hpp>
#include <toast/world/spotlight.hpp>
#include <toast/world/workspace_events.hpp>
#include <tracy/Tracy.hpp>
#include <tuple>
#include <unordered_set>

#if defined(_WIN32) && defined(TOAST_DEV_TOOLS)
#include <external/inc/nsight/NGFX_GPUTrace_Vulkan.h>
#include <external/inc/nsight/NGFX_GraphicsCapture_Vulkan.h>
#endif

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace renderer {

namespace {

[[nodiscard]]
auto defaultVoxelPalette() -> const voxel::Palette& {
	return voxel::defaultPalette();
}

[[nodiscard]]
auto voxelFragmentRenderId(physics::ShapeID shape) -> uint64_t {
	return 0xF7A6'1D00'0000'0000ull ^ ((static_cast<uint64_t>(shape.slot) << 32) | shape.generation);
}

[[nodiscard]]
auto packedGlassBricks(const voxel::gpu::PackedScene& scene) -> std::vector<std::vector<uint32_t>> {
	std::vector<std::vector<uint32_t>> out(scene.records.size());
	for (uint32_t record = 0; record < scene.records.size(); ++record) {
		out[record] = voxel::gpu::glassBricks(scene, record);
	}
	return out;
}

auto brickCoord(const voxel::gpu::VolumeRecord& record, uint32_t index) -> uint32_t {
	const uint32_t x = index % record.brick_dims_x;
	const uint32_t y = (index / record.brick_dims_x) % record.brick_dims_y;
	const uint32_t z = index / (record.brick_dims_x * record.brick_dims_y);
	return x | (y << k_brick_coord_bits) | (z << (2 * k_brick_coord_bits));
}

auto hashBricks(const VoxelBrickList& list) -> uint64_t {
	uint64_t hash = 14695981039346656037ull;
	const auto mix = [&hash](uint32_t word) { hash = (hash ^ word) * 1099511628211ull; };
	for (const uint32_t brick : list.occupied) {
		mix(brick);
	}
	mix(0xFFFFFFFFu);
	for (const uint32_t brick : list.glass) {
		mix(brick);
	}
	mix(list.dims.x);
	mix(list.dims.y);
	mix(list.dims.z);
	return hash;
}

/// Records no delta touched share the list of @p previous
[[nodiscard]]
auto packedBrickLists(
    const voxel::gpu::PackedScene& scene, const std::vector<std::vector<uint32_t>>& glass, const VoxelGpuStorage& storage,
    const VoxelGpuStorage* previous, std::span<const VolumeDelta> deltas
) -> std::vector<std::shared_ptr<const VoxelBrickList>> {
	std::unordered_set<uint64_t> dirty;
	for (const VolumeDelta& delta : deltas) {
		if (delta.unknown || !delta.bricks.empty()) {
			dirty.insert(delta.node_rid);
		}
	}

	std::vector<std::shared_ptr<const VoxelBrickList>> out(scene.records.size());
	for (uint32_t r = 0; r < scene.records.size(); ++r) {
		const voxel::gpu::VolumeRecord& record = scene.records[r];
		auto list = std::make_shared<VoxelBrickList>();
		list->dims = glm::uvec3(record.brick_dims_x, record.brick_dims_y, record.brick_dims_z);
		for (const uint32_t index : glass[r]) {
			list->glass.push_back(brickCoord(record, index));
		}

		const uint64_t rid = storage.nodeRid(r);
		const auto kept = previous != nullptr && rid != 0 && !dirty.contains(rid) ? previous->recordIndexOf(rid) : std::nullopt;
		const VoxelBrickList* old = kept.has_value() ? previous->brickList(*kept).get() : nullptr;
		if (old != nullptr && old->dims != list->dims) {
			old = nullptr;
		}
		if (old != nullptr && old->glass == list->glass) {
			out[r] = previous->brickList(*kept);
			continue;
		}
		if (old != nullptr) {
			list->occupied = old->occupied;
		} else {
			const uint32_t count = record.brick_dims_x * record.brick_dims_y * record.brick_dims_z;
			for (uint32_t i = 0; i < count; ++i) {
				if (voxel::BrickEntry {scene.grids[record.grid_offset + i]}.tag() != voxel::BrickTag::empty) {
					list->occupied.push_back(brickCoord(record, i));
				}
			}
		}
		list->hash = hashBricks(*list);
		out[r] = std::move(list);
	}
	return out;
}

}

auto VulkanRenderer::voxelCollisionWorld() const -> std::shared_ptr<const VoxelCollisionWorld> {
	// i need a way of getting this on the physics thread without the engine exploding so just lock it for a sec
	// i don't think this is that bad of an idea but maybe it's like bad idk
	std::scoped_lock lock(m_voxel_collision_mutex);
	return m_voxel_collision_world;
}

void VulkanRenderer::buildVoxelProxies(RenderFrame& frame) {
	ZoneScoped;

	auto& nodes = m_tick_voxel_nodes;
	{
		std::scoped_lock lock(m_voxel_proxy_mutex);
		nodes.assign(m_voxel_proxy_nodes.begin(), m_voxel_proxy_nodes.end());
	}

	if (m_voxel_storage_pending) {
		if (m_voxel_storage_pending->isReady()) {
			m_voxel_storage = std::move(m_voxel_storage_pending);
			m_voxel_storage_pending.reset();
			m_voxel_change_tracker.published();
		} else if (m_voxel_storage_pending->hasFailed()) {
			if (!m_voxel_upload_failed_warned) {
				m_voxel_upload_failed_warned = true;
				TOAST_ERROR("Render", "Voxel scene upload failed; voxel volumes draw from the previous upload, if any");
			}
			m_voxel_storage_pending.reset();
			m_voxel_patcher.reset();
		}
	}

	struct Gathered {
		uint64_t id;
		const voxel::Volume* volume;
		const voxel::Palette* palette;
		glm::mat4 model;
		std::string debug_name;
		bool data_only = false;
		uint64_t highlight_id = 0;
		const toast::VoxelNode* node = nullptr;
		uint64_t part_key = 0;
		uint64_t outline_id = 0;
	};

	// chatty vibed this
	const auto part_render_id = [](uint64_t node_rid, uint64_t part_key) {
		uint64_t mixed = (node_rid * 0x9E3779B97F4A7C15ull) ^ ((part_key + 0x7F4A7C15ull) * 0xC2B2AE3D27D4EB4Full);
		mixed ^= mixed >> 29;
		return (mixed & 0x3FFFFFFFFFFFFFFFull) | (uint64_t {1} << 62);
	};

	// Highlights share the voxel grid of their node so the shader looks up the same voxel
	constexpr uint64_t k_highlight_id_bit = uint64_t {1} << 63;

	std::vector<Gathered> gathered;
	std::vector<VoxelSceneKey> key;
	gathered.reserve(nodes.size());
	key.reserve(nodes.size());

	std::unique_lock voxel_lock {voxel::runtimePoolMutex()};

	for (auto* node : nodes) {
		if (node == nullptr || !node->enabled()) {
			continue;
		}
		const glm::mat4* owner_transform = ownerTransform(*node);
		if (owner_transform == nullptr) {
			continue;
		}

		if (node->usesParts()) {
			const voxel::Palette* parts_palette = node->resolvedPalette();
			if (parts_palette == nullptr) {
				parts_palette = &defaultVoxelPalette();
			}
			for (const toast::VoxelPart& part : node->parts()) {
				if (part.volume == nullptr) {
					continue;
				}
				glm::mat4 pose = node->getWorldTransform();
				if (part.has_pose) {
					pose = glm::translate(glm::mat4(1.0f), part.pose_position) * glm::mat4_cast(part.pose_rotation);
				}
				const uint64_t id = part_render_id(node->rid(), part.key);
				// clang-format off
				gathered.push_back(Gathered {
					.id = id,
					.volume = part.volume.get(),
					.palette = parts_palette,
					.model = *owner_transform * physics::Simulator::renderPoseDelta(part) * pose * part.localTransform(),
					.debug_name = std::string{node->name()},
					.node = node,
					.part_key = part.key,
					.outline_id = node->rid()
				});
				key.push_back({
					.node_rid = id,
					.revision = part.revision,
					.content = part.volume->revision(),
					.palette = parts_palette,
					.palette_revision = parts_palette->revision
				});
				// clang-format on
			}
			continue;
		}

		voxel::Volume* volume = node->volume();
		if (volume == nullptr) {
			continue;
		}
		const voxel::Palette* palette = node->resolvedPalette();
		if (palette == nullptr) {
			palette = &defaultVoxelPalette();
		}

		// clang-format off
		gathered.push_back(Gathered {
			.id = node->rid(),
			.volume = volume,
			.palette = palette,
			.model = *owner_transform * physics::Simulator::renderPoseDelta(*node) * node->getWorldTransform() * node->volumeLocalTransform(),
			.debug_name = std::string{node->name()},
			.node = node
		});
		key.push_back({
			.node_rid = node->rid(),
			.revision = node->revision(),
			.content = volume->revision(),
			.palette = palette,
			.palette_revision = palette->revision
		});
		// clang-format on
	}

	{
		std::scoped_lock lock(m_voxel_proxy_mutex);
		const size_t drawn = gathered.size();
		for (size_t i = 0; i < drawn; ++i) {
			auto it = m_voxel_highlights.find(VoxelHighlightKey {gathered[i].node, gathered[i].part_key});
			if (gathered[i].node == nullptr || it == m_voxel_highlights.end() ||
			    it->second->brickDims() != gathered[i].volume->brickDims()) {
				continue;
			}
			const uint64_t id = gathered[i].id ^ k_highlight_id_bit;
			gathered[i].highlight_id = id;
			gathered.push_back({
			  .id = id,
			  .volume = it->second,
			  .palette = gathered[i].palette,
			  .model = gathered[i].model,
			  .debug_name = gathered[i].debug_name + " highlight",
			  .data_only = true,
			});
			key.push_back(
			    {.node_rid = id,
					 .revision = 0,
					 .content = it->second->revision(),
					 .palette = gathered[i].palette,
					 .palette_revision = gathered[i].palette->revision}
			);
		}
	}

	for (const physics::VoxelRenderRecord& record : physics::Simulator::voxelFragmentRecords()) {
		if (record.volume == nullptr) {
			continue;
		}
		const voxel::Palette* palette = record.palette != nullptr ? record.palette : &defaultVoxelPalette();
		const uint64_t id = voxelFragmentRenderId(record.shape);

		const glm::mat4 drawn_transform = physics::Simulator::renderPoseDelta(record.body) * record.transform;
		gathered.push_back({id, record.volume, palette, drawn_transform, "Voxel fragment " + std::to_string(record.shape.slot)});
		key.push_back(
		    {.node_rid = id,
				 .revision = record.revision,
				 .content = record.volume->revision(),
				 .palette = palette,
				 .palette_revision = palette->revision}
		);
	}

	const bool keep_mirror = voxel_debug::isView(m_render_mode);

	// A pack still uploading is left alone rather than replaced every frame
	if (m_voxel_storage_pending == nullptr && (key != m_voxel_scene_key || keep_mirror != m_voxel_mirror_kept)) {
		std::vector<TrackedVolume> tracked;
		tracked.reserve(gathered.size());
		for (const Gathered& entry : gathered) {
			tracked.push_back({.node_rid = entry.id, .volume = entry.volume});
		}
		std::vector<VolumeDelta> deltas;
		m_voxel_change_tracker.observe(
		    tracked,
		    [this](uint64_t node_rid) -> std::optional<glm::uvec3> {
			    const auto record = m_voxel_storage ? m_voxel_storage->recordIndexOf(node_rid) : std::nullopt;
			    return record.has_value() ? std::optional {m_voxel_storage->recordBrickDims(*record)} : std::nullopt;
		    },
		    &deltas
		);

		m_voxel_scene_key = key;
		m_voxel_mirror_kept = keep_mirror;
		m_voxel_upload_failed_warned = false;

		if (gathered.empty()) {
			m_voxel_storage.reset();
			m_voxel_storage_pending.reset();
			m_voxel_change_tracker.reset();
			m_voxel_patcher.reset();
		} else {
			std::vector<voxel::gpu::SceneVolume> scene_volumes;
			std::vector<uint64_t> node_rids;
			std::vector<glm::uvec3> brick_dims;
			VoxelStorageDebugInfo debug;
			scene_volumes.reserve(gathered.size());
			node_rids.reserve(gathered.size());
			brick_dims.reserve(gathered.size());
			debug.node_names.reserve(gathered.size());
			for (const Gathered& entry : gathered) {
				scene_volumes.push_back({.volume = entry.volume, .palette = entry.palette});
				node_rids.push_back(entry.id);
				brick_dims.push_back(entry.volume->brickDims());
				debug.node_names.emplace_back(entry.debug_name);
			}

			const auto pack_start = std::chrono::steady_clock::now();
			const auto elapsed_ms = [&pack_start] {
				return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pack_start).count();
			};

			// Carves spawns and despawns patch the shared buffers in place and only running out of spare room uploads
			// everything again
			std::optional<VoxelScenePatcher::Patched> patched;
			if (!keep_mirror && m_voxel_storage != nullptr && m_voxel_storage->isReady()) {
				patched = m_voxel_patcher.tryPatch(voxel::runtimeBrickPool(), scene_volumes, node_rids, deltas);
			}
			std::optional<uint64_t> patch_sequence;
			if (patched.has_value()) {
				patch_sequence = m_voxel_storage->queuePatch(*m_core, patched->patch);
			}

			if (patch_sequence.has_value()) {
				debug.pack_ms = elapsed_ms();
				voxel_lock.unlock();

				const voxel::gpu::ScenePatch& patch = patched->patch;
				const VoxelStorageDebugInfo& previous = m_voxel_storage->debugInfo();
				debug.sequence = ++m_voxel_upload_sequence;
				debug.packed_slots = patch.pool_slots;
				debug.packed_palettes = patch.palette_words / voxel::gpu::k_palette_words;

				// Debug rows follow records and a dead record keeps an empty name
				std::unordered_map<uint64_t, std::string> names;
				for (size_t i = 0; i < node_rids.size(); ++i) {
					names.emplace(node_rids[i], std::move(debug.node_names[i]));
				}
				debug.node_names.assign(patched->record_rids.size(), std::string {});
				for (size_t i = 0; i < patched->record_rids.size(); ++i) {
					if (const auto name = names.find(patched->record_rids[i]); name != names.end()) {
						debug.node_names[i] = std::move(name->second);
					}
				}
				debug.bricks = previous.bricks;
				debug.bricks.resize(patched->record_rids.size());
				for (size_t i = 0; i < debug.bricks.size() && i < patch.tags.size(); ++i) {
					const auto apply = [](uint32_t count, int32_t delta) {
						return static_cast<uint32_t>(static_cast<int64_t>(count) + delta);
					};
					debug.bricks[i].uniform = apply(debug.bricks[i].uniform, patch.tags[i].uniform);
					debug.bricks[i].shared = apply(debug.bricks[i].shared, patch.tags[i].shared);
					debug.bricks[i].owned = apply(debug.bricks[i].owned, patch.tags[i].owned);
				}
				debug.patched = true;
				debug.patched_bricks = static_cast<uint32_t>(
				    std::accumulate(deltas.begin(), deltas.end(), size_t {0}, [](size_t sum, const VolumeDelta& delta) {
					    return sum + delta.bricks.size();
				    })
				);

				auto storage = std::make_shared<VoxelGpuStorage>(
				    std::move(patched->record_rids), std::move(patched->record_dims), *m_voxel_storage, *patch_sequence
				);
				auto glass = packedGlassBricks(m_voxel_patcher.scene());
				storage->setBrickLists(packedBrickLists(m_voxel_patcher.scene(), glass, *storage, m_voxel_storage.get(), deltas));
				storage->setGlassBricks(std::move(glass));
				storage->setDebugInfo(std::move(debug));
				storage->setChangeHistory(m_voxel_change_tracker.packed(storage->generation()));
				m_voxel_storage = std::move(storage);
				m_voxel_change_tracker.published();
			} else {
				auto packed = std::make_shared<VoxelPackedScene>(VoxelPackedScene {
				  .pool = voxel::gpu::packPool(voxel::runtimeBrickPool()), .scene = voxel::gpu::packScene(scene_volumes)
				});
				debug.pack_ms = elapsed_ms();
				voxel_lock.unlock();

				debug.sequence = ++m_voxel_upload_sequence;
				debug.packed_slots = static_cast<uint32_t>(packed->pool.materials.size() / voxel::gpu::k_material_words_per_brick);
				debug.packed_palettes = static_cast<uint32_t>(packed->scene.palettes.size() / voxel::gpu::k_palette_words);
				debug.bricks.reserve(packed->scene.records.size());
				for (const voxel::gpu::VolumeRecord& record : packed->scene.records) {
					VoxelStorageDebugInfo::BrickCensus census;
					const uint32_t count = record.brick_dims_x * record.brick_dims_y * record.brick_dims_z;
					for (uint32_t i = 0; i < count; ++i) {
						switch (voxel::BrickEntry {packed->scene.grids[record.grid_offset + i]}.tag()) {
							case voxel::BrickTag::uniform: ++census.uniform; break;
							case voxel::BrickTag::shared: ++census.shared; break;
							case voxel::BrickTag::owned: ++census.owned; break;
							case voxel::BrickTag::empty: break;
						}
					}
					debug.bricks.push_back(census);
				}
				if (keep_mirror) {
					debug.mirror = packed;
				}

				m_voxel_patcher.adopt(packed->scene, debug.packed_slots, node_rids);

				auto storage = std::make_shared<VoxelGpuStorage>(std::move(node_rids), std::move(brick_dims));
				auto glass = packedGlassBricks(packed->scene);
				storage->setBrickLists(packedBrickLists(packed->scene, glass, *storage, nullptr, {}));
				storage->setGlassBricks(std::move(glass));
				storage->setDebugInfo(std::move(debug));
				storage->setChangeHistory(m_voxel_change_tracker.packed(storage->generation()));
				queueResourceUpload(std::make_unique<VoxelSceneUpload>(storage, std::move(packed)));
				m_voxel_storage_pending = std::move(storage);
			}
		}
	}

	if (voxel_lock.owns_lock()) {
		voxel_lock.unlock();
	}

	frame.voxel_storage = m_voxel_storage;
	if (!m_voxel_storage) {
		m_voxel_previous_models.clear();
		m_voxel_drawn_models.clear();
		std::scoped_lock lock(m_voxel_collision_mutex);
		m_voxel_collision_world.reset();
		return;
	}

	// 1 + the packed record of a highlight so 0 stays none
	const auto highlight_record_of = [this](uint64_t id) -> uint32_t {
		const std::optional<uint32_t> record = id != 0 ? m_voxel_storage->recordIndexOf(id) : std::nullopt;
		return record.has_value() ? *record + 1u : 0u;
	};

	std::unordered_map<uint64_t, glm::mat4> drawn_models;
	drawn_models.reserve(gathered.size());
	frame.voxel_instances.reserve(gathered.size());

	for (const Gathered& entry : gathered) {
		const uint64_t node_rid = entry.id;
		if (entry.data_only) {
			continue;
		}
		const std::optional<uint32_t> record = m_voxel_storage->recordIndexOf(node_rid);
		if (!record.has_value()) {
			continue;
		}

		const glm::uvec3 dims = m_voxel_storage->recordBrickDims(*record);
		const glm::mat4 model = entry.model;
		const glm::vec4 sphere = voxel::worldBoundingSphere(model, dims);
		const auto previous = m_voxel_previous_models.find(node_rid);
		const bool has_previous = previous != m_voxel_previous_models.end();

		frame.voxel_instances.push_back(
		    VoxelVolumeProxy {
		      .record_index = *record,
		      .brick_dims = dims,
		      .model = model,
		      .inverse_model = glm::inverse(model),
		      .previous_model = has_previous ? previous->second : model,
		      .has_previous = has_previous,
		      .bounds_center = glm::vec3(sphere),
		      .bounds_radius = sphere.w,
		      .mirrored = glm::determinant(model) < 0.0f,
		      .node_rid = node_rid,
		      .highlight_record = highlight_record_of(entry.highlight_id),
		      .outlined = isOutlined(entry.outline_id != 0 ? entry.outline_id : node_rid),
		    }
		);
		drawn_models.insert_or_assign(node_rid, model);
	}

	auto collision = std::make_shared<VoxelCollisionWorld>();
	collision->storage = m_voxel_storage;
	collision->volumes.reserve(frame.voxel_instances.size());
	const glm::mat4 to_voxel_units = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / voxel::k_voxel_size));
	for (const VoxelVolumeProxy& proxy : frame.voxel_instances) {
		collision->volumes.push_back(
		    VoxelCollisionWorld::Volume {
		      .world_to_voxel = to_voxel_units * proxy.inverse_model,
		      .bounds_center = proxy.bounds_center,
		      .bounds_radius = proxy.bounds_radius,
		      .record_index = proxy.record_index,
		    }
		);
	}
	{
		std::scoped_lock lock(m_voxel_collision_mutex);
		m_voxel_collision_world = std::move(collision);
	}

	m_voxel_drawn_models = std::move(drawn_models);
}

}
