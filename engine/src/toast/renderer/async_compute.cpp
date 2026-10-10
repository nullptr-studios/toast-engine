/// @file async_compute.cpp
/// @author dario
/// @date 09/10/2026

#include "async_compute.hpp"

#include <algorithm>
#include <exception>
#include <string>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

namespace renderer {

AsyncCompute::AsyncCompute(const vk::raii::Device& device, uint32_t queue_family_index, vk::Queue queue, std::mutex* queue_mutex)
    : m_device(&device),
      m_queue_family_index(queue_family_index),
      m_queue(queue),
      m_queue_mutex(queue_mutex) {
	vk::SemaphoreTypeCreateInfo type_ci {};
	type_ci.semaphoreType = vk::SemaphoreType::eTimeline;
	type_ci.initialValue = k_no_ticket;

	vk::SemaphoreCreateInfo semaphore_ci {};
	semaphore_ci.pNext = &type_ci;
	m_timeline = vk::raii::Semaphore(device, semaphore_ci);

	TOAST_INFO("Render", "Async compute ready on queue family {}", queue_family_index);
}

AsyncCompute::~AsyncCompute() {
	// No queue lock since the core may already have destroyed it
	const Ticket last = lastSubmittedTicket();
	if (last != k_no_ticket && !wait(last)) {
		TOAST_WARN("Render", "Async compute shut down with ticket {} unfinished", last);
	}
}

void AsyncCompute::reclaimLocked() {
	const Ticket completed = m_timeline.getCounterValue();
	for (auto it = m_in_flight.begin(); it != m_in_flight.end();) {
		if (it->ticket <= completed) {
			m_free.push_back(std::move(*it));
			it = m_in_flight.erase(it);
		} else {
			++it;
		}
	}
}

auto AsyncCompute::acquireSlot() -> Slot {
	{
		std::scoped_lock lock(m_mutex);
		reclaimLocked();
		if (!m_free.empty()) {
			Slot slot = std::move(m_free.back());
			m_free.pop_back();
			slot.pool.reset();
			return slot;
		}
	}

	// One pool per slot so threads record without sharing a pool
	Slot slot;
	slot.pool = vk::raii::CommandPool(
	    *m_device, vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient, m_queue_family_index)
	);
	auto buffers = m_device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(*slot.pool, vk::CommandBufferLevel::ePrimary, 1));
	slot.cmd = std::move(buffers[0]);
	return slot;
}

auto AsyncCompute::submit(const std::function<void(vk::CommandBuffer)>& record, std::string_view debug_name) -> Ticket {
	ZoneScoped;
	Slot slot = acquireSlot();
	const vk::CommandBuffer cmd = *slot.cmd;

	try {
		cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		record(cmd);
		cmd.end();
	} catch (...) {
		std::scoped_lock lock(m_mutex);
		m_free.push_back(std::move(slot));
		throw;
	}

	vk::CommandBufferSubmitInfo command_info {};
	command_info.commandBuffer = cmd;

	vk::SemaphoreSubmitInfo signal {};
	signal.semaphore = *m_timeline;
	signal.stageMask = vk::PipelineStageFlagBits2::eAllCommands;

	vk::SubmitInfo2 info {};
	info.setCommandBufferInfos(command_info);
	info.setSignalSemaphoreInfos(signal);

	std::scoped_lock lock(m_mutex);
	// Same lock as the submit so values reach the queue in increasing order
	const Ticket ticket = m_last_submitted + 1;
	signal.value = ticket;
	{
		std::unique_lock<std::mutex> queue_lock;
		if (m_queue_mutex != nullptr) {
			queue_lock = std::unique_lock<std::mutex>(*m_queue_mutex);
		}
		m_queue.submit2(info, nullptr);
	}
	m_last_submitted = ticket;

	if (!debug_name.empty()) {
		TOAST_TRACE("Render", "Async compute job '{}' submitted as ticket {}", debug_name, ticket);
	}

	slot.ticket = ticket;
	m_in_flight.push_back(std::move(slot));
	return ticket;
}

auto AsyncCompute::isComplete(Ticket ticket) const -> bool {
	return ticket == k_no_ticket || m_timeline.getCounterValue() >= ticket;
}

auto AsyncCompute::wait(Ticket ticket, uint64_t timeout_ns) const -> bool {
	if (isComplete(ticket)) {
		return true;
	}
	const vk::Semaphore semaphore = *m_timeline;
	const vk::SemaphoreWaitInfo wait_info({}, semaphore, ticket);
	return m_device->waitSemaphores(wait_info, timeout_ns) == vk::Result::eSuccess;
}

auto AsyncCompute::completedTicket() const -> Ticket {
	return m_timeline.getCounterValue();
}

auto AsyncCompute::lastSubmittedTicket() const -> Ticket {
	std::scoped_lock lock(m_mutex);
	return m_last_submitted;
}

auto AsyncCompute::sharedFamilies(uint32_t graphics_family) const noexcept -> SharedFamilies {
	if (graphics_family == m_queue_family_index) {
		return {
		  .indices = {graphics_family, graphics_family},
        .count = 1
		};
	}
	return {
	  .indices = {graphics_family, m_queue_family_index},
      .count = 2
	};
}

}
