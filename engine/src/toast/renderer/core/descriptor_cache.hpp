/**
 * @file descriptor_cache.hpp
 * @author dario
 * @date 06/11/2026
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace renderer {

namespace descriptor_cache_detail {
inline std::atomic<uint64_t> target_generation {1};
}

[[nodiscard]]
inline auto targetGeneration() noexcept -> uint64_t {
	return descriptor_cache_detail::target_generation.load(std::memory_order_acquire);
}

/// Call whenever renderer owned targets are recreated since a new view can reuse a destroyed handle value
inline void bumpTargetGeneration() noexcept {
	descriptor_cache_detail::target_generation.fetch_add(1, std::memory_order_acq_rel);
}

/// What each descriptor set slot was last written with
template<typename Key>
class DescriptorCache {
public:
	DescriptorCache() = default;

	explicit DescriptorCache(size_t slots) : m_entries(slots) { }

	void resize(size_t slots) { m_entries.assign(slots, Entry {}); }

	[[nodiscard]]
	auto size() const noexcept -> size_t {
		return m_entries.size();
	}

	/// True when the slot must be rewritten and from then on counts it as written with key
	[[nodiscard]]
	auto update(size_t slot, const Key& key) -> bool {
		Entry& entry = m_entries[slot];
		const uint64_t generation = targetGeneration();
		if (entry.generation == generation && entry.key == key) {
			return false;
		}
		entry.key = key;
		entry.generation = generation;
		return true;
	}

	/// True when every slot was written since the last invalidation or target recreation
	[[nodiscard]]
	auto allCurrent() const noexcept -> bool {
		const uint64_t generation = targetGeneration();
		for (const Entry& entry : m_entries) {
			if (entry.generation != generation) {
				return false;
			}
		}
		return true;
	}

	void invalidate() noexcept {
		for (Entry& entry : m_entries) {
			entry.generation = 0;
		}
	}

private:
	struct Entry {
		Key key {};
		/// Zero is never a live generation so a fresh entry always writes
		uint64_t generation = 0;
	};

	std::vector<Entry> m_entries;
};

}
