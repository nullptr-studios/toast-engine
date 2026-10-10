/// @file gpu_sync.cpp
/// @author dario
/// @date 09/10/2026

#include "gpu_sync.hpp"

namespace renderer::sync {

namespace {

using Stage = vk::PipelineStageFlagBits2;
using Access = vk::AccessFlagBits2;
using Layout = vk::ImageLayout;

/// Only writes need making available since a read only needs the execution dependency
constexpr vk::AccessFlags2 k_write_access = Access::eShaderWrite | Access::eShaderStorageWrite | Access::eColorAttachmentWrite |
                                            Access::eDepthStencilAttachmentWrite | Access::eTransferWrite | Access::eHostWrite |
                                            Access::eMemoryWrite | Access::eAccelerationStructureWriteKHR;

auto makeImageBarrier(vk::Image image, const vk::ImageSubresourceRange& range, Usage previous, Usage next)
    -> vk::ImageMemoryBarrier2 {
	const UsageInfo src = info(previous);
	const UsageInfo dst = info(next);

	vk::ImageMemoryBarrier2 barrier {};
	barrier.srcStageMask = src.stages;
	barrier.srcAccessMask = src.access & k_write_access;
	barrier.dstStageMask = dst.stages;
	barrier.dstAccessMask = dst.access;
	barrier.oldLayout = src.layout;
	barrier.newLayout = dst.layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange = range;
	return barrier;
}

/// A read in a new stage still needs a barrier since the write was only made visible to the first read stages
auto readCoveredBy(Usage previous, Usage next) -> bool {
	const UsageInfo src = info(previous);
	const UsageInfo dst = info(next);
	return (dst.stages & src.stages) == dst.stages && (dst.access & src.access) == dst.access;
}

auto needsImageBarrier(Usage previous, Usage next) -> bool {
	if (next == Usage::none) {
		return false;
	}
	if (writes(previous) || writes(next) || info(previous).layout != info(next).layout) {
		return true;
	}
	return !readCoveredBy(previous, next);
}

auto needsBufferBarrier(Usage previous, Usage next) -> bool {
	if (next == Usage::none || previous == Usage::none) {
		return false;
	}
	return writes(previous) || writes(next) || !readCoveredBy(previous, next);
}

}

auto info(Usage usage) -> UsageInfo {
	switch (usage) {
		case Usage::none: return {Stage::eNone, Access::eNone, Layout::eUndefined};

		case Usage::color_attachment:
			return {
			  Stage::eColorAttachmentOutput,
			  Access::eColorAttachmentRead | Access::eColorAttachmentWrite,
			  Layout::eColorAttachmentOptimal,
			};
		case Usage::depth_attachment:
			return {
			  Stage::eEarlyFragmentTests | Stage::eLateFragmentTests,
			  Access::eDepthStencilAttachmentRead | Access::eDepthStencilAttachmentWrite,
			  Layout::eDepthAttachmentOptimal,
			};
		case Usage::depth_sampled: return {Stage::eFragmentShader, Access::eShaderSampledRead, Layout::eDepthReadOnlyOptimal};

		case Usage::fragment_sampled: return {Stage::eFragmentShader, Access::eShaderSampledRead, Layout::eShaderReadOnlyOptimal};
		case Usage::compute_sampled: return {Stage::eComputeShader, Access::eShaderSampledRead, Layout::eShaderReadOnlyOptimal};
		case Usage::shader_sampled:
			return {
			  Stage::eVertexShader | Stage::eFragmentShader | Stage::eComputeShader,
			  Access::eShaderSampledRead,
			  Layout::eShaderReadOnlyOptimal,
			};

		case Usage::compute_storage_read: return {Stage::eComputeShader, Access::eShaderStorageRead, Layout::eGeneral};
		case Usage::compute_storage_write:
			return {Stage::eComputeShader, Access::eShaderStorageRead | Access::eShaderStorageWrite, Layout::eGeneral};
		case Usage::vertex_storage_read: return {Stage::eVertexShader, Access::eShaderStorageRead, Layout::eGeneral};
		case Usage::fragment_storage_read: return {Stage::eFragmentShader, Access::eShaderStorageRead, Layout::eGeneral};
		case Usage::uniform_read:
			return {
			  Stage::eVertexShader | Stage::eFragmentShader | Stage::eComputeShader,
			  Access::eUniformRead,
			  Layout::eUndefined,
			};
		case Usage::vertex_input:
			return {
			  Stage::eVertexAttributeInput | Stage::eIndexInput,
			  Access::eVertexAttributeRead | Access::eIndexRead,
			  Layout::eUndefined,
			};
		case Usage::indirect_read: return {Stage::eDrawIndirect, Access::eIndirectCommandRead, Layout::eUndefined};

		case Usage::transfer_src: return {Stage::eTransfer, Access::eTransferRead, Layout::eTransferSrcOptimal};
		case Usage::transfer_dst: return {Stage::eTransfer, Access::eTransferWrite, Layout::eTransferDstOptimal};
		case Usage::host_read: return {Stage::eHost, Access::eHostRead, Layout::eGeneral};
		case Usage::host_write: return {Stage::eHost, Access::eHostWrite, Layout::eGeneral};

		case Usage::present: return {Stage::eColorAttachmentOutput, Access::eNone, Layout::ePresentSrcKHR};

		case Usage::acceleration_build:
			return {
			  Stage::eAccelerationStructureBuildKHR,
			  Access::eAccelerationStructureReadKHR | Access::eAccelerationStructureWriteKHR,
			  Layout::eUndefined,
			};
		case Usage::acceleration_trace_fragment:
			return {Stage::eFragmentShader, Access::eAccelerationStructureReadKHR, Layout::eUndefined};

		case Usage::general: return {Stage::eAllCommands, Access::eMemoryRead | Access::eMemoryWrite, Layout::eGeneral};
	}
	return {Stage::eAllCommands, Access::eMemoryRead | Access::eMemoryWrite, Layout::eGeneral};
}

auto writes(Usage usage) -> bool {
	return static_cast<bool>(info(usage).access & k_write_access);
}

void BarrierBatch::image(ImageState& state, Usage next) {
	if (!state) {
		return;
	}
	image(state.image, state.range, state.usage, next);
	state.usage = next;
}

void BarrierBatch::image(vk::Image image, const vk::ImageSubresourceRange& range, Usage previous, Usage next) {
	if (image == VK_NULL_HANDLE || !needsImageBarrier(previous, next)) {
		return;
	}
	m_images.push_back(makeImageBarrier(image, range, previous, next));
}

void BarrierBatch::buffer(vk::Buffer buffer, Usage previous, Usage next, vk::DeviceSize offset, vk::DeviceSize size) {
	if (buffer == VK_NULL_HANDLE || size == 0 || !needsBufferBarrier(previous, next)) {
		return;
	}
	const UsageInfo src = info(previous);
	const UsageInfo dst = info(next);

	vk::BufferMemoryBarrier2 barrier {};
	barrier.srcStageMask = src.stages;
	barrier.srcAccessMask = src.access & k_write_access;
	barrier.dstStageMask = dst.stages;
	barrier.dstAccessMask = dst.access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer;
	barrier.offset = offset;
	barrier.size = size;
	m_buffers.push_back(barrier);
}

void BarrierBatch::memory(Usage previous, Usage next) {
	if (!needsBufferBarrier(previous, next)) {
		return;
	}
	const UsageInfo src = info(previous);
	const UsageInfo dst = info(next);

	vk::MemoryBarrier2 barrier {};
	barrier.srcStageMask = src.stages;
	barrier.srcAccessMask = src.access & k_write_access;
	barrier.dstStageMask = dst.stages;
	barrier.dstAccessMask = dst.access;
	m_memory.push_back(barrier);
}

void BarrierBatch::release(
    vk::Image image, const vk::ImageSubresourceRange& range, Usage previous, Usage next, const QueueHandoff& handoff
) {
	if (!handoff.crossesQueues()) {
		this->image(image, range, previous, next);
		return;
	}
	if (image == VK_NULL_HANDLE) {
		return;
	}
	vk::ImageMemoryBarrier2 barrier = makeImageBarrier(image, range, previous, next);
	// The consumer acquire carries the destination scope
	barrier.dstStageMask = Stage::eNone;
	barrier.dstAccessMask = Access::eNone;
	if (handoff.exclusive) {
		barrier.srcQueueFamilyIndex = handoff.from;
		barrier.dstQueueFamilyIndex = handoff.to;
	}
	m_images.push_back(barrier);
}

void BarrierBatch::acquire(
    vk::Image image, const vk::ImageSubresourceRange& range, Usage previous, Usage next, const QueueHandoff& handoff
) {
	if (!handoff.crossesQueues() || image == VK_NULL_HANDLE) {
		return;
	}
	vk::ImageMemoryBarrier2 barrier = makeImageBarrier(image, range, previous, next);
	// The release carried the source scope and the fence wait orders this after it
	barrier.srcStageMask = Stage::eNone;
	barrier.srcAccessMask = Access::eNone;
	if (handoff.exclusive) {
		barrier.srcQueueFamilyIndex = handoff.from;
		barrier.dstQueueFamilyIndex = handoff.to;
	} else {
		// The release already transitioned a concurrent image
		barrier.oldLayout = barrier.newLayout;
	}
	m_images.push_back(barrier);
}

void BarrierBatch::release(
    vk::Buffer buffer, Usage previous, Usage next, const QueueHandoff& handoff, vk::DeviceSize offset, vk::DeviceSize size
) {
	if (!handoff.crossesQueues()) {
		this->buffer(buffer, previous, next, offset, size);
		return;
	}
	if (buffer == VK_NULL_HANDLE || size == 0) {
		return;
	}
	const UsageInfo src = info(previous);

	vk::BufferMemoryBarrier2 barrier {};
	barrier.srcStageMask = src.stages;
	barrier.srcAccessMask = src.access & k_write_access;
	barrier.dstStageMask = Stage::eNone;
	barrier.dstAccessMask = Access::eNone;
	barrier.srcQueueFamilyIndex = handoff.exclusive ? handoff.from : VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = handoff.exclusive ? handoff.to : VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer;
	barrier.offset = offset;
	barrier.size = size;
	m_buffers.push_back(barrier);
}

void BarrierBatch::acquire(
    vk::Buffer buffer, Usage next, const QueueHandoff& handoff, vk::DeviceSize offset, vk::DeviceSize size
) {
	if (!handoff.crossesQueues() || buffer == VK_NULL_HANDLE || size == 0) {
		return;
	}
	const UsageInfo dst = info(next);

	vk::BufferMemoryBarrier2 barrier {};
	barrier.srcStageMask = Stage::eNone;
	barrier.srcAccessMask = Access::eNone;
	barrier.dstStageMask = dst.stages;
	barrier.dstAccessMask = dst.access;
	barrier.srcQueueFamilyIndex = handoff.exclusive ? handoff.from : VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = handoff.exclusive ? handoff.to : VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer;
	barrier.offset = offset;
	barrier.size = size;
	m_buffers.push_back(barrier);
}

void BarrierBatch::flush(vk::CommandBuffer cmd) {
	if (empty()) {
		return;
	}

	vk::DependencyInfo dependency {};
	dependency.memoryBarrierCount = static_cast<uint32_t>(m_memory.size());
	dependency.pMemoryBarriers = m_memory.data();
	dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(m_buffers.size());
	dependency.pBufferMemoryBarriers = m_buffers.data();
	dependency.imageMemoryBarrierCount = static_cast<uint32_t>(m_images.size());
	dependency.pImageMemoryBarriers = m_images.data();
	cmd.pipelineBarrier2(dependency);

	m_images.clear();
	m_buffers.clear();
	m_memory.clear();
}

void transition(vk::CommandBuffer cmd, vk::Image image, const vk::ImageSubresourceRange& range, Usage previous, Usage next) {
	BarrierBatch batch;
	batch.image(image, range, previous, next);
	batch.flush(cmd);
}

void transition(vk::CommandBuffer cmd, ImageState& state, Usage next) {
	BarrierBatch batch;
	batch.image(state, next);
	batch.flush(cmd);
}

void submit(vk::Queue queue, const SubmitDesc& desc) {
	std::vector<vk::SemaphoreSubmitInfo> waits;
	waits.reserve(desc.waits.size());
	for (const auto& wait : desc.waits) {
		waits.push_back(vk::SemaphoreSubmitInfo {}.setSemaphore(wait.semaphore).setStageMask(wait.stages));
	}

	std::vector<vk::SemaphoreSubmitInfo> signals;
	signals.reserve(desc.signals.size());
	for (const vk::Semaphore semaphore : desc.signals) {
		signals.push_back(vk::SemaphoreSubmitInfo {}.setSemaphore(semaphore).setStageMask(vk::PipelineStageFlagBits2::eAllCommands));
	}

	std::vector<vk::CommandBufferSubmitInfo> commands;
	commands.reserve(desc.commands.size());
	for (const vk::CommandBuffer cmd : desc.commands) {
		commands.push_back(vk::CommandBufferSubmitInfo {}.setCommandBuffer(cmd));
	}

	vk::SubmitInfo2 info {};
	info.pNext = desc.next;
	info.setWaitSemaphoreInfos(waits);
	info.setCommandBufferInfos(commands);
	info.setSignalSemaphoreInfos(signals);
	queue.submit2(info, desc.fence);
}

}
