#include "surface.hpp"

#include <cassert>

namespace voxel {

auto VolumeSurface::contains(glm::ivec3 brick) const noexcept -> bool {
	return brick.x >= 0 && brick.y >= 0 && brick.z >= 0 && std::cmp_less(brick.x, m_brick_dims.x) &&
	       std::cmp_less(brick.y, m_brick_dims.y) && std::cmp_less(brick.z, m_brick_dims.z);
}

auto VolumeSurface::slotOf(glm::ivec3 brick) const noexcept -> uint32_t {
	assert(contains(brick));
	return static_cast<uint32_t>(brick.x) + (static_cast<uint32_t>(brick.y) * m_brick_dims.x) +
	       (static_cast<uint32_t>(brick.z) * m_brick_dims.x * m_brick_dims.y);
}

void VolumeSurface::rebuild(const Volume& volume) {
	ZoneScopedN("voxel::RebuildSurface");
	m_brick_dims = volume.brickDims();
	m_bricks.clear();

	volume.forEachSolidBrick([&](glm::ivec3 brick, uint32_t) { rebuildBrick(volume, brick); });
}

void VolumeSurface::rebuildBrick(const Volume& volume, glm::ivec3 brick) {
	assert(volume.brickDims() == m_brick_dims && "rebuild() against this volume first");
	if (!contains(brick)) {
		return;
	}

	const uint32_t slot = slotOf(brick);
	const BrickOccupancy* occupancy = volume.occupancyPointer(brick);
	if (occupancy == nullptr) {
		m_bricks.erase(slot);
		return;
	}

	thread_local std::vector<SurfaceVoxel> scratch;
	scratch.clear();
	buildBrickSurfaceInto(*occupancy, volume.neighbourhoodOf(brick), scratch);
	const auto it = m_bricks.find(slot);
	if (scratch.empty()) {
		if (it != m_bricks.end()) {
			m_bricks.erase(it);
		}
		return;
	}
	// A neighbour repaired for nothing keeps its list, and with it the one every copy of this surface shares
	if (it != m_bricks.end() && *it->second == scratch) {
		return;
	}
	auto list = std::make_shared<const std::vector<SurfaceVoxel>>(scratch);
	if (it != m_bricks.end()) {
		it->second = std::move(list);
	} else {
		m_bricks.emplace(slot, std::move(list));
	}
}

void VolumeSurface::repairAround(const Volume& volume, glm::ivec3 voxel) {
	ZoneScopedN("voxel::RepairAround");
	if (!volume.containsVoxel(voxel)) {
		return;
	}

	const glm::ivec3 brick = brickOf(voxel);
	const glm::ivec3 local {voxel.x & 7, voxel.y & 7, voxel.z & 7};
	const int32_t last = static_cast<int32_t>(k_brick_dim) - 1;

	rebuildBrick(volume, brick);
	for (int32_t axis = 0; axis < 3; ++axis) {
		glm::ivec3 step {0};
		step[axis] = 1;
		if (local[axis] == 0) {
			rebuildBrick(volume, brick - step);
		} else if (local[axis] == last) {
			rebuildBrick(volume, brick + step);
		}
	}
}

void VolumeSurface::repairBrickRegion(const Volume& volume, glm::ivec3 brick) {
	ZoneScopedN("voxel::RepairBrickRegion");
	rebuildBrick(volume, brick);
	for (int32_t axis = 0; axis < 3; ++axis) {
		glm::ivec3 step {0};
		step[axis] = 1;
		rebuildBrick(volume, brick - step);
		rebuildBrick(volume, brick + step);
	}
}

void VolumeSurface::repairBricks(const Volume& volume, std::span<const glm::ivec3> dirty) {
	ZoneScopedN("voxel::RepairBricks");

	std::vector<glm::ivec3> unique_bricks;
	unique_bricks.reserve(dirty.size() * 7);
	for (const glm::ivec3& brick : dirty) {
		unique_bricks.push_back(brick);
		for (int32_t axis = 0; axis < 3; ++axis) {
			glm::ivec3 step {0};
			step[axis] = 1;
			unique_bricks.push_back(brick - step);
			unique_bricks.push_back(brick + step);
		}
	}
	std::ranges::sort(unique_bricks, {}, [](const glm::ivec3& b) { return std::tuple(b.x, b.y, b.z); });
	unique_bricks.erase(std::unique(unique_bricks.begin(), unique_bricks.end()), unique_bricks.end());

	ZoneValue(static_cast<uint64_t>(unique_bricks.size()));
	for (const glm::ivec3& brick : unique_bricks) {
		rebuildBrick(volume, brick);
	}
}

auto VolumeSurface::brickSurface(glm::ivec3 brick) const -> std::span<const SurfaceVoxel> {
	if (!contains(brick)) {
		return {};
	}
	const auto it = m_bricks.find(slotOf(brick));
	if (it == m_bricks.end()) {
		return {};
	}
	return {*it->second};
}

auto VolumeSurface::surfaceVoxelCount() const -> size_t {
	size_t total = 0;
	for (const auto& [slot, list] : m_bricks) {
		total += list->size();
	}
	return total;
}

}
