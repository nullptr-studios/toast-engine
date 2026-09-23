#include "surface.hpp"

#include <algorithm>
#include <cassert>
#include <tracy/Tracy.hpp>
#include <tuple>
#include <utility>

namespace voxel {

namespace {

/// @brief Arithmetic shift so voxel -1 lands in brick -1
[[nodiscard]]
auto brickContaining(glm::ivec3 voxel) noexcept -> glm::ivec3 {
	return {voxel.x >> 3, voxel.y >> 3, voxel.z >> 3};
}

}

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

	for (int32_t z = 0; std::cmp_less(z, m_brick_dims.z); ++z) {
		for (int32_t y = 0; std::cmp_less(y, m_brick_dims.y); ++y) {
			for (int32_t x = 0; std::cmp_less(x, m_brick_dims.x); ++x) {
				rebuildBrick(volume, glm::ivec3 {x, y, z});
			}
		}
	}
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

	std::vector<SurfaceVoxel>& list = m_bricks[slot];
	list.clear();
	buildBrickSurfaceInto(*occupancy, volume.neighbourhoodOf(brick), list);
	if (list.empty()) {
		m_bricks.erase(slot);
	}
}

void VolumeSurface::repairAround(const Volume& volume, glm::ivec3 voxel) {
	ZoneScopedN("voxel::RepairAround");
	if (!volume.containsVoxel(voxel)) {
		return;
	}

	const glm::ivec3 brick = brickContaining(voxel);
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
	return {it->second};
}

auto VolumeSurface::surfaceVoxelCount() const -> size_t {
	size_t total = 0;
	for (const auto& [slot, list] : m_bricks) {
		total += list.size();
	}
	return total;
}

}
