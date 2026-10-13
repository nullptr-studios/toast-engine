/// @file async_compute.hpp
/// @author dario
/// @date 09/10/2026

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <string_view>
#include <toast/export.hpp>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace renderer {

class TOAST_API AsyncCompute {
public:
	/// Timeline value so completing one completes every earlier one
	using Ticket = uint64_t;

	static constexpr Ticket k_no_ticket = 0;

	/// @param queue_mutex non null when another subsystem submits to the same VkQueue
	AsyncCompute(const vk::raii::Device& device, uint32_t queue_family_index, vk::Queue queue, std::mutex* queue_mutex);

	~AsyncCompute();

	AsyncCompute(const AsyncCompute&) = delete;
	auto operator=(const AsyncCompute&) -> AsyncCompute& = delete;
	AsyncCompute(AsyncCompute&&) = delete;
	auto operator=(AsyncCompute&&) -> AsyncCompute& = delete;

	/// Thread safe and records outside the lock so threads record in parallel
	/// An exception from @p record propagates and no ticket is handed out
	auto submit(const std::function<void(vk::CommandBuffer)>& record, std::string_view debug_name = {}) -> Ticket;

	[[nodiscard]]
	auto isComplete(Ticket ticket) const -> bool;

	/// @return false on timeout
	auto wait(Ticket ticket, uint64_t timeout_ns = std::numeric_limits<uint64_t>::max()) const -> bool;

	[[nodiscard]]
	auto completedTicket() const -> Ticket;

	[[nodiscard]]
	auto lastSubmittedTicket() const -> Ticket;

	[[nodiscard]]
	auto queueFamilyIndex() const noexcept -> uint32_t {
		return m_queue_family_index;
	}

	/// Never wait on it from a frame submit or the frame couples to async work again
	[[nodiscard]]
	auto timeline() const noexcept -> vk::Semaphore {
		return *m_timeline;
	}

	/// For eConcurrent create infos with count entries since one family repeats
	struct SharedFamilies {
		std::array<uint32_t, 2> indices {};
		uint32_t count = 0;
	};

	[[nodiscard]]
	auto sharedFamilies(uint32_t graphics_family) const noexcept -> SharedFamilies;

private:
	struct Slot {
		vk::raii::CommandPool pool = nullptr;
		vk::raii::CommandBuffer cmd = nullptr;
		Ticket ticket = k_no_ticket;
	};

	/// Caller holds m_mutex
	void reclaimLocked();
	auto acquireSlot() -> Slot;

	const vk::raii::Device* m_device = nullptr;
	uint32_t m_queue_family_index = 0;
	vk::Queue m_queue = nullptr;
	std::mutex* m_queue_mutex = nullptr;

	vk::raii::Semaphore m_timeline = nullptr;

	mutable std::mutex m_mutex;
	Ticket m_last_submitted = k_no_ticket;
	std::vector<Slot> m_in_flight;
	std::vector<Slot> m_free;
};

}
