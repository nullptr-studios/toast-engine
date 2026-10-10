/// @file async_compute_task.cpp
/// @author dario
/// @date 10/10/2026

#include "async_compute_task.hpp"

#include "shader_cache.hpp"
#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <thread>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

constexpr vk::BufferUsageFlags k_task_buffer_usage =
    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;

}

void AsyncComputeTask::StepContext::dispatchRaw(
    uint32_t entry, std::array<uint32_t, 3> groups, const void* push, uint32_t push_size
) {
	if (entry >= m_task.m_pipelines.size() || !m_task.m_pipelines[entry]->isReady()) {
		return;
	}

	// Dispatches in one command buffer do not order their storage writes
	if (m_dispatched) {
		sync::BarrierBatch barriers;
		barriers.memory(sync::Usage::compute_storage_write, sync::Usage::compute_storage_write);
		barriers.flush(m_cmd);
	}
	m_dispatched = true;

	const vk::PipelineLayout layout = *m_task.m_layout.getPipelineLayout();
	m_cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_task.m_pipelines[entry]->getPipeline());
	m_cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, layout, 0, {*m_task.m_slots[m_slot].set}, {});
	if (push != nullptr && push_size > 0) {
		m_cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, push_size, push);
	}
	m_cmd.dispatch(groups[0], groups[1], groups[2]);
}

auto AsyncComputeTask::FrameView::buffer(BufferId id) const -> vk::Buffer {
	if (m_task == nullptr || id >= m_task->m_buffers.size() || !m_task->m_buffers[id].ring) {
		return nullptr;
	}
	return *m_task->m_buffers[id].copies[m_slot];
}

AsyncComputeTask::AsyncComputeTask(Config config) : m_config(std::move(config)), m_core(&getCore()) {
	ZoneScoped;
	m_frame_slot.fill(-1);

	const auto uid = assets::resolveURI(m_config.shader_uri);
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "{} shader {} unavailable", m_config.debug_name, m_config.shader_uri);
		return;
	}

	m_layout.rebuild(*m_core, shader->reflection, m_config.debug_name);
	for (const std::string& entry : m_config.entries) {
		VulkanPipeline::Config pipeline_config;
		pipeline_config.pipeline_type = VulkanPipeline::PipelineType::compute;
		pipeline_config.debug_name = std::format("{} {}", m_config.debug_name, entry);
		pipeline_config.shader_spirv = shader->spirv;
		pipeline_config.pipeline_layout = *m_layout.getPipelineLayout();
		pipeline_config.compute_entry = entry;
		auto& pipeline = m_pipelines.emplace_back(std::make_unique<VulkanPipeline>());
		pipeline->rebuild(*m_core, pipeline_config);
	}
	m_ready = !m_pipelines.empty() && std::ranges::all_of(m_pipelines, [](const auto& p) { return p->isReady(); });
}

AsyncComputeTask::~AsyncComputeTask() {
	// Buffers and sets must outlive every job that uses them
	const AsyncCompute& compute = m_core->asyncCompute();
	auto unused = compute.wait(compute.lastSubmittedTicket());
}

/// This is superturbobad do not overuse  it
auto AsyncComputeTask::addRingBuffer(vk::DeviceSize size, bool cpu_readback) -> BufferId {
	const auto shared = m_core->asyncCompute().sharedFamilies(m_core->getGraphicsQueueFamilyIndex());
	vk::BufferCreateInfo buffer_ci({}, size, k_task_buffer_usage);
	// Concurrent so the renderer reads it with a visibility barrier and no ownership transfer
	if (shared.count > 1) {
		buffer_ci.sharingMode = vk::SharingMode::eConcurrent;
		buffer_ci.queueFamilyIndexCount = shared.count;
		buffer_ci.pQueueFamilyIndices = shared.indices.data();
	}
	vma::AllocationCreateInfo device_alloc {};
	device_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;

	vk::BufferCreateInfo readback_ci({}, size, vk::BufferUsageFlagBits::eTransferDst);
	vma::AllocationCreateInfo readback_alloc {};
	readback_alloc.usage = vma::MemoryUsage::eAutoPreferHost;
	readback_alloc.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessRandom;

	const auto id = static_cast<BufferId>(m_buffers.size());
	Buffer& buffer = m_buffers.emplace_back();
	buffer.ring = true;
	buffer.cpu_readback = cpu_readback;
	buffer.size = size;
	for (uint32_t slot = 0; slot < k_slot_count; ++slot) {
		buffer.copies.push_back(m_core->getAllocator().createBuffer(buffer_ci, device_alloc));
		setDebugName(*m_core, *buffer.copies.back(), std::format("{} Ring{}[{}]", m_config.debug_name, id, slot));
		if (cpu_readback) {
			buffer.readbacks.push_back(m_core->getAllocator().createBuffer(readback_ci, readback_alloc));
		}
	}
	return id;
}

auto AsyncComputeTask::addPersistentBuffer(vk::DeviceSize size) -> BufferId {
	// Exclusive to the compute family since only this task touches it
	const vk::BufferCreateInfo buffer_ci({}, size, k_task_buffer_usage);
	vma::AllocationCreateInfo device_alloc {};
	device_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;

	const auto id = static_cast<BufferId>(m_buffers.size());
	Buffer& buffer = m_buffers.emplace_back();
	buffer.size = size;
	buffer.copies.push_back(m_core->getAllocator().createBuffer(buffer_ci, device_alloc));
	setDebugName(*m_core, *buffer.copies.back(), std::format("{} Persistent{}", m_config.debug_name, id));
	return id;
}

void AsyncComputeTask::bind(uint32_t binding, BufferId id, Copy copy) {
	m_bindings.push_back({.binding = binding, .id = id, .copy = copy});
}

void AsyncComputeTask::createDescriptors() {
	if (*m_pool || !m_ready) {
		return;
	}
	const auto& layouts = m_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		return;
	}

	const auto& device = m_core->getDevice();
	// Own pool since the renderer pool belongs to the render thread
	const std::array pool_sizes {vk::DescriptorPoolSize(
	    vk::DescriptorType::eStorageBuffer, std::max<uint32_t>(1, static_cast<uint32_t>(m_bindings.size())) * k_slot_count
	)};
	m_pool = vk::raii::DescriptorPool(
	    device, vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, k_slot_count, pool_sizes)
	);

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	for (Slot& slot : m_slots) {
		auto sets = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*m_pool, 1, &set_layout));
		slot.set = std::move(sets[0]);
	}
}

void AsyncComputeTask::writeDescriptors(uint32_t slot) {
	// Before the first step every ring copy holds the upload so any other slot works as previous
	const uint32_t previous = m_last_written >= 0 ? static_cast<uint32_t>(m_last_written) : (slot + 1) % k_slot_count;

	std::vector<vk::DescriptorBufferInfo> infos;
	infos.reserve(m_bindings.size());
	std::vector<vk::WriteDescriptorSet> writes;
	writes.reserve(m_bindings.size());
	for (const Binding& binding : m_bindings) {
		const Buffer& buffer = m_buffers[binding.id];
		const uint32_t copy = !buffer.ring ? 0 : (binding.copy == Copy::current ? slot : previous);
		infos.emplace_back(*buffer.copies[copy], 0, buffer.size);
		writes.emplace_back(*m_slots[slot].set, binding.binding, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &infos.back());
	}
	// The set of a claimed slot is not bound by any pending job so rewriting it is safe
	m_core->getDevice().updateDescriptorSets(writes, {});
}

void AsyncComputeTask::upload(BufferId id, std::span<const std::byte> data) {
	ZoneScoped;
	if (id >= m_buffers.size()) {
		return;
	}
	Buffer& buffer = m_buffers[id];
	if (buffer.ring && m_step_counter > 0) {
		TOAST_ERROR("Render", "{} ring buffer upload after the first step would race frames reading it", m_config.debug_name);
		return;
	}
	const vk::DeviceSize bytes = std::min<vk::DeviceSize>(data.size(), buffer.size);

	vk::BufferCreateInfo staging_ci({}, bytes, vk::BufferUsageFlagBits::eTransferSrc);
	vma::AllocationCreateInfo staging_alloc {};
	staging_alloc.usage = vma::MemoryUsage::eAutoPreferHost;
	staging_alloc.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
	vma::raii::Buffer staging = m_core->getAllocator().createBuffer(staging_ci, staging_alloc);
	std::memcpy(staging.getAllocation().getInfo().pMappedData, data.data(), bytes);
	staging.getAllocation().flush(0, bytes);

	AsyncCompute& compute = m_core->asyncCompute();
	const AsyncCompute::Ticket ticket = compute.submit(
	    [&](vk::CommandBuffer cmd) {
		    // Earlier steps may still read or write these copies
		    sync::BarrierBatch barriers;
		    barriers.memory(sync::Usage::compute_storage_write, sync::Usage::transfer_dst);
		    barriers.flush(cmd);
		    for (const vma::raii::Buffer& copy : buffer.copies) {
			    cmd.copyBuffer(*staging, *copy, vk::BufferCopy(0, 0, bytes));
		    }
	    },
	    std::format("{} upload", m_config.debug_name)
	);
	// Simplest correct lifetime for the staging buffer and uploads are rare
	auto unused = compute.wait(ticket);
}

void AsyncComputeTask::step(const std::function<void(StepContext&)>& record, std::string_view debug_name) {
	ZoneScoped;
	if (!m_ready) {
		return;
	}
	createDescriptors();

	const uint32_t slot = claimFreeSlot();
	writeDescriptors(slot);

	const AsyncCompute::Ticket ticket = m_core->asyncCompute().submit(
	    [&](vk::CommandBuffer cmd) {
		    // Submits on one queue may overlap so the previous step and any upload must land first
		    sync::BarrierBatch barriers;
		    barriers.memory(sync::Usage::compute_storage_write, sync::Usage::compute_storage_write);
		    barriers.memory(sync::Usage::transfer_dst, sync::Usage::compute_storage_write);
		    barriers.flush(cmd);

		    StepContext context(*this, cmd, slot);
		    record(context);

		    // CPU copies of this step for read()
		    bool any_readback = false;
		    for (const Buffer& buffer : m_buffers) {
			    if (buffer.cpu_readback) {
				    barriers.buffer(*buffer.copies[slot], sync::Usage::compute_storage_write, sync::Usage::transfer_src);
				    any_readback = true;
			    }
		    }
		    if (!any_readback) {
			    return;
		    }
		    barriers.flush(cmd);
		    for (const Buffer& buffer : m_buffers) {
			    if (buffer.cpu_readback) {
				    cmd.copyBuffer(*buffer.copies[slot], *buffer.readbacks[slot], vk::BufferCopy(0, 0, buffer.size));
				    barriers.buffer(*buffer.readbacks[slot], sync::Usage::transfer_dst, sync::Usage::host_read);
			    }
		    }
		    barriers.flush(cmd);
	    },
	    debug_name.empty() ? std::string_view(m_config.debug_name) : debug_name
	);

	publish(slot, ticket);
}

auto AsyncComputeTask::read(BufferId id, std::span<std::byte> out) -> bool {
	if (id >= m_buffers.size() || !m_buffers[id].cpu_readback) {
		return false;
	}
	int32_t newest = -1;
	{
		std::scoped_lock lock(m_mutex);
		newest = newestFinishedLocked();
	}
	// Only the owner thread rewrites slots so it stays valid after the lock
	if (newest < 0) {
		return false;
	}
	const vma::raii::Buffer& readback = m_buffers[id].readbacks[static_cast<size_t>(newest)];
	const vk::DeviceSize bytes = std::min<vk::DeviceSize>(out.size(), m_buffers[id].size);
	// Non coherent host memory needs an invalidate before the CPU sees device writes
	readback.getAllocation().invalidate(0, bytes);
	std::memcpy(out.data(), readback.getAllocation().getInfo().pMappedData, bytes);
	return true;
}

auto AsyncComputeTask::acquireForFrame(vk::CommandBuffer cmd, uint32_t frame_index, sync::Usage next) -> FrameView {
	if (frame_index >= m_frame_slot.size()) {
		return {};
	}

	int32_t newest = -1;
	uint64_t step = 0;
	{
		std::scoped_lock lock(m_mutex);
		if (const int32_t old = m_frame_slot[frame_index]; old >= 0) {
			--m_slots[static_cast<size_t>(old)].pins;
			m_frame_slot[frame_index] = -1;
		}
		newest = newestFinishedLocked();
		if (newest < 0) {
			return {};
		}
		++m_slots[static_cast<size_t>(newest)].pins;
		m_frame_slot[frame_index] = newest;
		step = m_slots[static_cast<size_t>(newest)].step;
	}
	const auto slot = static_cast<uint32_t>(newest);

	if (m_acquired_step[slot] != step) {
		const sync::QueueHandoff handoff {
		  .from = m_core->asyncCompute().queueFamilyIndex(), .to = m_core->getGraphicsQueueFamilyIndex(), .exclusive = false
		};
		sync::BarrierBatch barriers;
		for (const Buffer& buffer : m_buffers) {
			if (buffer.ring) {
				barriers.acquire(*buffer.copies[slot], next, handoff);
			}
		}
		barriers.flush(cmd);
		m_acquired_step[slot] = step;
	}

	FrameView view;
	view.m_task = this;
	view.m_slot = slot;
	view.m_step = step;
	return view;
}

auto AsyncComputeTask::finishedStep() const -> uint64_t {
	std::scoped_lock lock(m_mutex);
	const int32_t newest = newestFinishedLocked();
	return newest < 0 ? 0 : m_slots[static_cast<size_t>(newest)].step;
}

auto AsyncComputeTask::claimFreeSlot() -> uint32_t {
	const AsyncCompute& compute = m_core->asyncCompute();
	while (true) {
		{
			std::scoped_lock lock(m_mutex);
			// Never the next step input, the result the renderer shows next or a slot a frame still binds
			const int32_t newest = newestFinishedLocked();
			for (uint32_t i = 0; i < k_slot_count; ++i) {
				Slot& slot = m_slots[i];
				const auto index = static_cast<int32_t>(i);
				if (index != m_last_written && index != newest && slot.pins == 0 && compute.isComplete(slot.ticket)) {
					slot.step = 0;
					return i;
				}
			}
		}
		// Every slot busy means the GPU is behind or frames still bind them, thats bad
		if (compute.completedTicket() < compute.lastSubmittedTicket()) {
			auto unused = compute.wait(compute.completedTicket() + 1);
		} else {
			std::this_thread::yield();
		}
	}
}

void AsyncComputeTask::publish(uint32_t slot, AsyncCompute::Ticket ticket) {
	std::scoped_lock lock(m_mutex);
	m_slots[slot].ticket = ticket;
	m_slots[slot].step = ++m_step_counter;
	m_last_written = static_cast<int32_t>(slot);
}

auto AsyncComputeTask::newestFinishedLocked() const -> int32_t {
	const AsyncCompute& compute = m_core->asyncCompute();
	int32_t best = -1;
	uint64_t best_step = 0;
	for (uint32_t i = 0; i < k_slot_count; ++i) {
		const Slot& slot = m_slots[i];
		if (slot.step > best_step && compute.isComplete(slot.ticket)) {
			best = static_cast<int32_t>(i);
			best_step = slot.step;
		}
	}
	return best;
}

}
