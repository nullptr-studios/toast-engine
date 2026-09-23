/// @file gpu_timer.cpp

#include "gpu_timer.hpp"

#include "vulkan_core.hpp"

#include <algorithm>
#include <toast/log.hpp>

namespace renderer {

namespace {

constexpr double k_smoothing = 0.1;

auto validBitsMask(uint32_t bits) noexcept -> uint64_t {
	return bits >= 64 ? ~uint64_t {0} : (uint64_t {1} << bits) - 1;
}

auto smooth(double previous, double sample, bool first) noexcept -> double {
	return first ? sample : previous + ((sample - previous) * k_smoothing);
}

auto sameScope(const GpuTimer::ScopeTiming& a, const GpuTimer::ScopeName& name, uint32_t depth) noexcept -> bool {
	return a.depth == depth && a.name.view() == name.view();
}

}    // namespace

GpuTimer::ScopeName::ScopeName(std::string_view text) noexcept
    : length(static_cast<uint8_t>(std::min(text.size(), chars.size()))) {
	std::copy_n(text.data(), length, chars.data());
}

GpuTimer::GpuTimer(const VulkanCore& core, uint32_t frame_slots) {
	if (frame_slots == 0) {
		return;
	}

	const auto& physical_device = core.getPhysicalDevice();
	const float period = physical_device.getProperties().limits.timestampPeriod;
	const auto families = physical_device.getQueueFamilyProperties();

	const uint32_t graphics_family = core.getGraphicsQueueFamilyIndex();
	const uint32_t compute_family = core.getComputeQueueFamilyIndex();
	const uint32_t graphics_bits = graphics_family < families.size() ? families[graphics_family].timestampValidBits : 0;
	const uint32_t compute_bits = compute_family < families.size() ? families[compute_family].timestampValidBits : 0;

	if (period <= 0.0f || graphics_bits == 0) {
		TOAST_INFO("Render", "No GPU timestamp queries on this device; the performance overlay shows CPU timings only");
		return;
	}

	vk::QueryPoolCreateInfo create_info {};
	create_info.queryType = vk::QueryType::eTimestamp;
	create_info.queryCount = frame_slots * k_queries_per_slot;
	m_pool = vk::raii::QueryPool(core.getDevice(), create_info);

	m_slots.resize(frame_slots);
	for (Slot& slot : m_slots) {
		slot.scopes.reserve(k_max_scopes);
	}

	m_ns_per_tick = static_cast<double>(period);
	m_graphics_mask = validBitsMask(graphics_bits);
	m_compute_mask = validBitsMask(compute_bits);
	m_supported = true;
	m_compute_supported = compute_bits > 0;
}

void GpuTimer::beginGraphics(uint32_t slot, vk::CommandBuffer cmd) {
	if (!m_supported || slot >= m_slots.size()) {
		return;
	}
	Slot& state = m_slots[slot];
	state.scopes.clear();
	state.depth = 0;
	state.graphics_written = false;

	// The compute pair is reset by the compute command buffer
	cmd.resetQueryPool(*m_pool, base(slot), k_queries_per_slot - 2);
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *m_pool, base(slot));
}

void GpuTimer::endGraphics(uint32_t slot, vk::CommandBuffer cmd) {
	if (!m_supported || slot >= m_slots.size()) {
		return;
	}
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *m_pool, base(slot) + 1);
	m_slots[slot].graphics_written = true;
}

void GpuTimer::beginCompute(uint32_t slot, vk::CommandBuffer cmd) {
	if (!m_compute_supported || slot >= m_slots.size()) {
		return;
	}
	m_slots[slot].compute_written = false;

	const uint32_t first = base(slot) + k_queries_per_slot - 2;
	cmd.resetQueryPool(*m_pool, first, 2);
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *m_pool, first);
}

void GpuTimer::endCompute(uint32_t slot, vk::CommandBuffer cmd) {
	if (!m_compute_supported || slot >= m_slots.size()) {
		return;
	}
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *m_pool, base(slot) + k_queries_per_slot - 1);
	m_slots[slot].compute_written = true;
}

auto GpuTimer::beginScope(uint32_t slot, vk::CommandBuffer cmd, std::string_view name) -> uint32_t {
	if (!m_supported || slot >= m_slots.size()) {
		return k_no_scope;
	}
	Slot& state = m_slots[slot];
	if (state.scopes.size() >= k_max_scopes) {
		return k_no_scope;
	}

	const auto index = static_cast<uint32_t>(state.scopes.size());
	state.scopes.push_back({.name = ScopeName(name), .depth = state.depth, .cpu_start = std::chrono::steady_clock::now()});
	++state.depth;
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *m_pool, base(slot) + 2 + (2 * index));
	return index;
}

void GpuTimer::endScope(uint32_t slot, vk::CommandBuffer cmd, uint32_t scope) {
	if (scope == k_no_scope || slot >= m_slots.size()) {
		return;
	}
	Slot& state = m_slots[slot];
	if (scope >= state.scopes.size()) {
		return;
	}

	PendingScope& pending = state.scopes[scope];
	pending.cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pending.cpu_start).count();
	state.depth = pending.depth;
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *m_pool, base(slot) + 3 + (2 * scope));
}

auto GpuTimer::ticksToMs(uint64_t begin, uint64_t end, uint64_t mask) const noexcept -> double {
	// Masked so a counter narrower than 64 bits wraps correctly
	const uint64_t ticks = (end - begin) & mask;
	return static_cast<double>(ticks) * m_ns_per_tick / 1.0e6;
}

void GpuTimer::collect(uint32_t slot) {
	if (!m_supported || slot >= m_slots.size()) {
		return;
	}
	Slot& state = m_slots[slot];
	if (!state.graphics_written) {
		return;
	}
	state.graphics_written = false;

	const auto scope_count = static_cast<uint32_t>(state.scopes.size());
	const uint32_t count = 2 + (2 * scope_count);
	const auto graphics =
	    m_pool.getResults<uint64_t>(base(slot), count, count * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::e64);
	if (graphics.result != vk::Result::eSuccess) {
		if (!m_reported_unavailable) {
			m_reported_unavailable = true;
			TOAST_WARN("Render", "GPU timestamps not readable after the frame finished: {}", vk::to_string(graphics.result));
		}
		return;
	}
	const std::vector<uint64_t>& ticks = graphics.value;

	m_frame.valid = true;
	m_frame.graphics_ms = ticksToMs(ticks[0], ticks[1], m_graphics_mask);
	m_frame.compute_ms = 0.0;
	m_frame.scopes.clear();
	for (uint32_t i = 0; i < scope_count; ++i) {
		const PendingScope& pending = state.scopes[i];
		const double gpu_ms = ticksToMs(ticks[2 + (2 * i)], ticks[3 + (2 * i)], m_graphics_mask);

		const auto same = std::ranges::find_if(m_frame.scopes, [&pending](const ScopeTiming& timing) {
			return sameScope(timing, pending.name, pending.depth);
		});
		if (same != m_frame.scopes.end()) {
			++same->count;
			same->cpu_ms += pending.cpu_ms;
			same->gpu_ms += gpu_ms;
		} else {
			m_frame.scopes.push_back(
			    {.name = pending.name, .depth = pending.depth, .count = 1, .cpu_ms = pending.cpu_ms, .gpu_ms = gpu_ms}
			);
		}
	}

	if (m_compute_supported && state.compute_written) {
		state.compute_written = false;
		const uint32_t first = base(slot) + k_queries_per_slot - 2;
		const auto compute =
		    m_pool.getResults<uint64_t>(first, 2, 2 * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::e64);
		if (compute.result == vk::Result::eSuccess) {
			m_frame.compute_ms = ticksToMs(compute.value[0], compute.value[1], m_compute_mask);
		}
	}

	const bool first_frame = !m_smoothed.valid;
	m_scratch.valid = true;
	m_scratch.graphics_ms = smooth(m_smoothed.graphics_ms, m_frame.graphics_ms, first_frame);
	m_scratch.compute_ms = smooth(m_smoothed.compute_ms, m_frame.compute_ms, first_frame);
	m_scratch.scopes.clear();
	for (const ScopeTiming& sample : m_frame.scopes) {
		const auto previous = std::ranges::find_if(m_smoothed.scopes, [&sample](const ScopeTiming& timing) {
			return sameScope(timing, sample.name, sample.depth);
		});
		const bool seen = previous != m_smoothed.scopes.end();
		m_scratch.scopes.push_back(
		    {.name = sample.name,
				 .depth = sample.depth,
				 .count = sample.count,
				 .cpu_ms = smooth(seen ? previous->cpu_ms : 0.0, sample.cpu_ms, !seen),
				 .gpu_ms = smooth(seen ? previous->gpu_ms : 0.0, sample.gpu_ms, !seen)}
		);
	}
	std::swap(m_smoothed, m_scratch);
}

}
