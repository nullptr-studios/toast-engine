/// @file voxel_change_history.hpp
/// @author dario
/// @date 21/09/2026

#pragma once

#include "frustum.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <toast/voxel/voxel_volume.hpp>
#include <vector>

namespace renderer {

/// A box that changed in the voxel space of one volume
struct VoxelRegion {
	uint64_t node_uid = 0;
	glm::vec3 min {0.0f};
	glm::vec3 max {0.0f};
};

/// Regions each published voxel storage brought oldest first
class VoxelChangeHistory {
public:
	struct Entry {
		uint64_t generation = 0;
		std::shared_ptr<const std::vector<VoxelRegion>> regions;
	};

	static constexpr size_t k_max_entries = 32;

	[[nodiscard]]
	static auto extend(const VoxelChangeHistory& previous, uint64_t generation, std::vector<VoxelRegion> regions)
	    -> VoxelChangeHistory;

	/// @returns false when @p generation is not in the history so the caller must assume everything changed
	template<typename Visit>
	auto forEachSince(uint64_t generation, Visit&& visit) const -> bool {
		const auto known = std::ranges::find(m_entries, generation, &Entry::generation);
		if (known == m_entries.end()) {
			return false;
		}
		for (auto entry = std::next(known); entry != m_entries.end(); ++entry) {
			for (const VoxelRegion& region : *entry->regions) {
				visit(region);
			}
		}
		return true;
	}

private:
	std::vector<Entry> m_entries;
};

struct TrackedVolume {
	uint64_t node_uid = 0;
	const voxel::Volume* volume = nullptr;
};

/// Brick dims of a volume in the storage frames use or nullopt when it is not in there
using PublishedDims = std::function<std::optional<glm::uvec3>(uint64_t)>;

/// What one volume changed since the observe before
struct VolumeDelta {
	uint64_t node_uid = 0;

	/// New or replaced or its history was discarded so only a full pack describes it
	bool unknown = false;

	/// Grid indices of the bricks written
	std::vector<uint32_t> bricks;
};

/// Finds what changed in each volume between packs and keeps it until a storage carrying it is published
class VoxelChangeTracker {
public:
	/// Call with the voxel lock held since it reads and then discards the dirty bricks of every volume
	void observe(
	    std::span<const TrackedVolume> volumes, const PublishedDims& published_dims, std::vector<VolumeDelta>* deltas = nullptr
	);

	/// @returns the history the new storage carries
	auto packed(uint64_t generation) -> VoxelChangeHistory;

	/// The storage from the last packed() is now the one frames use
	void published();

	void reset();

private:
	struct Seen {
		uint64_t node_uid = 0;
		uint64_t volume_id = 0;
		uint32_t content = 0;
	};

	std::vector<Seen> m_seen;
	VoxelChangeHistory m_published;
	VoxelChangeHistory m_pending;
	std::vector<VoxelRegion> m_unpublished;
	bool m_has_pending = false;
};

struct VoxelCaster {
	uint64_t node_uid = 0;
	glm::mat4 model {1.0f};
};

/// True when a region published after @p generation reaches one of the views or when that generation is gone
[[nodiscard]]
auto regionsReach(
    const VoxelChangeHistory& history, uint64_t generation, std::span<const VoxelCaster> casters,
    std::span<const FrustumPlanes> views
) -> bool;

}
