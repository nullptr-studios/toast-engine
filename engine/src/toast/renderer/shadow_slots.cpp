/// @file shadow_slots.cpp
/// @author dario
/// @date 21/09/2026

#include "shadow_slots.hpp"

#include "shadow_constants.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <utility>

namespace renderer::shadow_slots {

namespace {

constexpr size_t k_max_slots = 8;

std::atomic<float> g_retention {k_default_retention};
std::atomic<uint32_t> g_grace_frames {k_default_grace_frames};

}

auto assign(std::span<const Candidate> candidates, std::span<Slot> slots, std::span<int32_t> slot_of) -> uint32_t {
	const size_t slot_count = std::min(slots.size(), k_max_slots);
	const float retention_factor = retention();
	const uint32_t grace_frames = graceFrames();
	std::ranges::fill(slot_of, -1);

	for (size_t s = 0; s < slot_count; ++s) {
		if (slots[s].key == 0) {
			continue;
		}
		const auto owner = std::ranges::find(candidates, slots[s].key, &Candidate::key);
		if (owner == candidates.end()) {
			slots[s] = {};
			continue;
		}
		slot_of[static_cast<size_t>(owner - candidates.begin())] = static_cast<int32_t>(s);
		slots[s].hidden_frames = owner->visible ? 0 : slots[s].hidden_frames + 1;
	}

	const auto rank = [&](size_t c) {
		const int32_t owned = slot_of[c];
		const bool seen = candidates[c].visible || (owned >= 0 && slots[static_cast<size_t>(owned)].hidden_frames <= grace_frames);
		return std::pair {seen, candidates[c].importance * (owned >= 0 ? retention_factor : 1.0f)};
	};

	// Best first and only as long as there are slots
	std::array<uint32_t, k_max_slots> winners {};
	size_t winner_count = 0;
	for (size_t c = 0; c < candidates.size(); ++c) {
		if (candidates[c].key == 0) {
			continue;
		}
		size_t position = winner_count;
		while (position > 0 && rank(c) > rank(winners[position - 1])) {
			--position;
		}
		if (position >= slot_count) {
			continue;
		}
		winner_count = std::min(winner_count + 1, slot_count);
		for (size_t i = winner_count - 1; i > position; --i) {
			winners[i] = winners[i - 1];
		}
		winners[position] = static_cast<uint32_t>(c);
	}

	std::array<int32_t, k_max_slots> winner_slot {};
	std::array<bool, k_max_slots> retained {};
	for (size_t w = 0; w < winner_count; ++w) {
		winner_slot[w] = slot_of[winners[w]];
		if (winner_slot[w] >= 0) {
			retained[static_cast<size_t>(winner_slot[w])] = true;
		}
	}
	std::ranges::fill(slot_of, -1);

	uint32_t displaced = 0;
	for (size_t s = 0; s < slot_count; ++s) {
		if (slots[s].key != 0 && !retained[s]) {
			slots[s] = {};
			++displaced;
		}
	}

	for (size_t w = 0; w < winner_count; ++w) {
		auto slot = static_cast<size_t>(std::max(winner_slot[w], 0));
		if (winner_slot[w] < 0) {
			slot = slot_count;
			for (size_t s = 0; s < slot_count && slot == slot_count; ++s) {
				if (slots[s].key == 0) {
					slot = s;
				}
			}
			if (slot == slot_count) {
				continue;
			}
			slots[slot] = Slot {.key = candidates[winners[w]].key};
		}
		slot_of[winners[w]] = static_cast<int32_t>(slot);
	}
	return displaced;
}

auto owns(std::span<const Slot> slots, uint64_t key) -> bool {
	return key != 0 && std::ranges::any_of(slots, [key](const Slot& slot) { return slot.key == key; });
}

auto retention() -> float {
	return g_retention.load(std::memory_order_relaxed);
}

auto graceFrames() -> uint32_t {
	return g_grace_frames.load(std::memory_order_relaxed);
}

void setRetention(float factor) {
	// Below 1 an owner would be easier to displace than a challenger
	g_retention.store(factor < 1.0f ? 1.0f : factor, std::memory_order_relaxed);
}

void setGraceFrames(uint32_t frames) {
	g_grace_frames.store(frames, std::memory_order_relaxed);
}

auto stableResolution(uint32_t current, float camera_distance, float resolution_scale) -> uint32_t {
	if (current == 0) {
		return shadows::punctualShadowResolution(camera_distance, resolution_scale);
	}
	const uint32_t nearer = shadows::punctualShadowResolution(camera_distance * (1.0f - k_resolution_band), resolution_scale);
	const uint32_t farther = shadows::punctualShadowResolution(camera_distance * (1.0f + k_resolution_band), resolution_scale);
	return std::clamp(current, farther, nearer);
}

}
