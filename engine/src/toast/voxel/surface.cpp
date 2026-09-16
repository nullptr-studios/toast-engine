#include "surface.hpp"

#include <cassert>
#include <tracy/Tracy.hpp>
#include <utility>

namespace toast::voxel {

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
	ZoneScoped;
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

	std::vector<SurfaceVoxel> list = buildBrickSurface(*occupancy, volume.neighbourhoodOf(brick));
	if (list.empty()) {
		m_bricks.erase(slot);
	} else {
		m_bricks[slot] = std::move(list);
	}
}

void VolumeSurface::repairAround(const Volume& volume, glm::ivec3 voxel) {
	ZoneScoped;
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
	rebuildBrick(volume, brick);
	for (int32_t axis = 0; axis < 3; ++axis) {
		glm::ivec3 step {0};
		step[axis] = 1;
		rebuildBrick(volume, brick - step);
		rebuildBrick(volume, brick + step);
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
