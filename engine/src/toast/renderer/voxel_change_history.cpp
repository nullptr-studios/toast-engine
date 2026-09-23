/// @file voxel_change_history.cpp
/// @author dario
/// @date 21/09/2026

#include "voxel_change_history.hpp"

#include <toast/voxel/volume_bounds.hpp>
#include <utility>

namespace renderer {

namespace {

constexpr size_t k_max_unpublished = 256;

constexpr int k_cell_bricks = 2;

constexpr size_t k_max_regions_per_volume = 128;

void mergePerVolume(std::vector<VoxelRegion>& regions) {
	std::ranges::sort(regions, {}, &VoxelRegion::node_uid);

	size_t merged = 0;
	for (size_t i = 0; i < regions.size(); ++i) {
		if (merged > 0 && regions[merged - 1].node_uid == regions[i].node_uid) {
			regions[merged - 1].min = glm::min(regions[merged - 1].min, regions[i].min);
			regions[merged - 1].max = glm::max(regions[merged - 1].max, regions[i].max);
		} else {
			regions[merged++] = regions[i];
		}
	}
	regions.resize(merged);
}

/// One box per cell of bricks and bigger cells for a large blast
void appendBrickRegions(
    std::vector<VoxelRegion>& out, uint64_t node_uid, const voxel::Volume& volume, std::span<const uint32_t> bricks
) {
	struct Item {
		uint64_t cell = 0;
		glm::ivec3 brick {0};
	};

	std::vector<Item> items(bricks.size());
	int cell_size = k_cell_bricks;

	for (;;) {
		for (size_t i = 0; i < bricks.size(); ++i) {
			const glm::ivec3 brick = volume.brickAtIndex(bricks[i]);
			const glm::ivec3 cell = brick / cell_size;
			items[i] = {
			  (static_cast<uint64_t>(cell.x) << 42) | (static_cast<uint64_t>(cell.y) << 21) | static_cast<uint64_t>(cell.z), brick
			};
		}
		std::ranges::sort(items, {}, &Item::cell);

		size_t cells = 0;
		for (size_t i = 0; i < items.size(); ++i) {
			cells += i == 0 || items[i].cell != items[i - 1].cell ? 1 : 0;
		}
		if (cells <= k_max_regions_per_volume) {
			break;
		}
		cell_size *= 2;
	}

	for (size_t first = 0; first < items.size();) {
		glm::ivec3 low = items[first].brick;
		glm::ivec3 high = low;
		size_t last = first;
		for (; last < items.size() && items[last].cell == items[first].cell; ++last) {
			low = glm::min(low, items[last].brick);
			high = glm::max(high, items[last].brick);
		}
		out.push_back(
		    {.node_uid = node_uid, .min = glm::vec3(low) * voxel::k_brick_size, .max = glm::vec3(high + 1) * voxel::k_brick_size}
		);
		first = last;
	}
}

}

auto VoxelChangeHistory::extend(const VoxelChangeHistory& previous, uint64_t generation, std::vector<VoxelRegion> regions)
    -> VoxelChangeHistory {
	VoxelChangeHistory history;

	const size_t keep = std::min(previous.m_entries.size(), k_max_entries - 1);
	history.m_entries.assign(previous.m_entries.end() - static_cast<std::ptrdiff_t>(keep), previous.m_entries.end());
	history.m_entries.push_back(
	    Entry {.generation = generation, .regions = std::make_shared<const std::vector<VoxelRegion>>(std::move(regions))}
	);
	return history;
}

void VoxelChangeTracker::observe(
    std::span<const TrackedVolume> volumes, const PublishedDims& published_dims, std::vector<VolumeDelta>* deltas
) {
	std::vector<Seen> now;
	now.reserve(volumes.size());
	if (deltas != nullptr) {
		deltas->assign(volumes.size(), VolumeDelta {});
	}

	for (size_t index = 0; index < volumes.size(); ++index) {
		const TrackedVolume& tracked = volumes[index];
		VolumeDelta scratch;
		VolumeDelta& delta = deltas != nullptr ? (*deltas)[index] : scratch;
		delta.node_uid = tracked.node_uid;

		const voxel::Volume& volume = *tracked.volume;
		const Seen current {.node_uid = tracked.node_uid, .volume_id = volume.id(), .content = volume.revision()};
		now.push_back(current);

		// New volumes are covered by the caster set of each group
		const auto previous = std::ranges::find(m_seen, tracked.node_uid, &Seen::node_uid);
		delta.unknown = previous == m_seen.end();
		if (previous != m_seen.end()) {
			const bool content_changed = previous->content != current.content;
			std::optional<std::span<const uint32_t>> dirty;
			if (content_changed && previous->volume_id == current.volume_id) {
				dirty = volume.dirtyBricksSince(previous->content);
			}

			if (previous->volume_id != current.volume_id || (content_changed && !dirty.has_value())) {
				delta.unknown = true;

				// The old shape may reach further than the new one
				glm::uvec3 dims = volume.brickDims();
				if (const auto old_dims = published_dims ? published_dims(tracked.node_uid) : std::nullopt) {
					dims = glm::max(dims, *old_dims);
				}
				m_unpublished.push_back({.node_uid = tracked.node_uid, .min = glm::vec3(0.0f), .max = voxel::localExtent(dims)});
			} else if (dirty.has_value() && !dirty->empty()) {
				appendBrickRegions(m_unpublished, tracked.node_uid, volume, *dirty);
				delta.bricks.assign(dirty->begin(), dirty->end());
			}
		}
		volume.discardDirty();
	}
	m_seen = std::move(now);
}

auto VoxelChangeTracker::packed(uint64_t generation) -> VoxelChangeHistory {
	if (m_unpublished.size() > k_max_unpublished) {
		mergePerVolume(m_unpublished);
	}

	m_pending = VoxelChangeHistory::extend(m_published, generation, m_unpublished);
	m_has_pending = true;
	return m_pending;
}

void VoxelChangeTracker::published() {
	if (!m_has_pending) {
		return;
	}
	m_published = std::move(m_pending);
	m_pending = {};
	m_unpublished.clear();
	m_has_pending = false;
}

void VoxelChangeTracker::reset() {
	m_seen.clear();
	m_published = {};
	m_pending = {};
	m_unpublished.clear();
	m_has_pending = false;
}

auto regionsReach(
    const VoxelChangeHistory& history, uint64_t generation, std::span<const VoxelCaster> casters,
    std::span<const FrustumPlanes> views
) -> bool {
	bool reached = false;
	const bool known = history.forEachSince(generation, [&](const VoxelRegion& region) {
		for (const VoxelCaster& caster : casters) {
			if (reached) {
				return;
			}
			if (caster.node_uid != region.node_uid) {
				continue;
			}
			const Box world = transformBox(caster.model, region.min, region.max);
			reached =
			    std::ranges::any_of(views, [&world](const FrustumPlanes& planes) { return boxTouchesLateralFrustum(planes, world); });
		}
	});
	return reached || !known;
}

}
