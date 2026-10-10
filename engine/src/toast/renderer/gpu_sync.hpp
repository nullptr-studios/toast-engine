/// @file gpu_sync.hpp
/// @author dario
/// @date 09/10/2026

#pragma once

#include <cstdint>
#include <span>
#include <toast/export.hpp>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace renderer::sync {

enum class Usage : uint8_t {
	/// Contents discarded and the layout undefined
	none,

	/// Includes load and store ops
	color_attachment,
	/// eDepthAttachmentOptimal including load and store ops
	depth_attachment,
	/// eDepthReadOnlyOptimal sampled by fragment shaders
	depth_sampled,

	fragment_sampled,
	compute_sampled,
	/// Vertex fragment and compute
	shader_sampled,

	compute_storage_read,
	compute_storage_write,
	/// Vertex pulling
	vertex_storage_read,
	fragment_storage_read,
	/// Any shader stage
	uniform_read,
	/// Vertex and index buffers
	vertex_input,
	indirect_read,

	transfer_src,
	transfer_dst,
	host_read,
	host_write,

	/// Colour attachment output stage so the next use chains onto the acquire semaphore wait
	present,

	/// Build input output and scratch
	acceleration_build,
	/// Fragment shader ray queries
	acceleration_trace_fragment,

	/// Every stage and access in eGeneral so correct anywhere and slow everywhere
	general,
};

struct UsageInfo {
	vk::PipelineStageFlags2 stages;
	vk::AccessFlags2 access;
	vk::ImageLayout layout = vk::ImageLayout::eUndefined;
};

[[nodiscard]]
TOAST_API auto info(Usage usage) -> UsageInfo;

[[nodiscard]]
TOAST_API auto writes(Usage usage) -> bool;

/// Owned next to the image it describes
struct ImageState {
	vk::Image image = VK_NULL_HANDLE;
	vk::ImageSubresourceRange range {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
	Usage usage = Usage::none;

	/// On every image creation since its contents start undefined
	void reset(vk::Image new_image, const vk::ImageSubresourceRange& new_range) {
		image = new_image;
		range = new_range;
		usage = Usage::none;
	}

	[[nodiscard]]
	explicit operator bool() const {
		return image != VK_NULL_HANDLE;
	}
};

struct QueueHandoff {
	uint32_t from = VK_QUEUE_FAMILY_IGNORED;
	uint32_t to = VK_QUEUE_FAMILY_IGNORED;
	/// False for eConcurrent resources which need visibility on the consumer but no ownership transfer
	bool exclusive = true;

	[[nodiscard]]
	auto crossesQueues() const noexcept -> bool {
		return from != to;
	}
};

/// Skips a barrier only when neither side writes and the layout and next read stages are already covered
class TOAST_API BarrierBatch {
public:
	/// No op for an image that does not exist. The widest usage until the next write avoids read to read barriers
	void image(ImageState& state, Usage next);

	void image(vk::Image image, const vk::ImageSubresourceRange& range, Usage previous, Usage next);

	void buffer(vk::Buffer buffer, Usage previous, Usage next, vk::DeviceSize offset = 0, vk::DeviceSize size = VK_WHOLE_SIZE);

	void memory(Usage previous, Usage next);

	/// On the writing queue. A plain transition within one family. Across families @p next may name stages this queue lacks
	/// since the acquire waits for them
	void release(vk::Image image, const vk::ImageSubresourceRange& range, Usage previous, Usage next, const QueueHandoff& handoff);

	/// On the consuming queue after the release fence was waited on. @p previous and @p next must match the release
	/// exactly and nothing is recorded within one family
	void acquire(vk::Image image, const vk::ImageSubresourceRange& range, Usage previous, Usage next, const QueueHandoff& handoff);

	void release(
	    vk::Buffer buffer, Usage previous, Usage next, const QueueHandoff& handoff, vk::DeviceSize offset = 0,
	    vk::DeviceSize size = VK_WHOLE_SIZE
	);

	void acquire(
	    vk::Buffer buffer, Usage next, const QueueHandoff& handoff, vk::DeviceSize offset = 0, vk::DeviceSize size = VK_WHOLE_SIZE
	);

	void flush(vk::CommandBuffer cmd);

	[[nodiscard]]
	auto empty() const -> bool {
		return m_images.empty() && m_buffers.empty() && m_memory.empty();
	}

	[[nodiscard]]
	auto pendingImages() const -> std::span<const vk::ImageMemoryBarrier2> {
		return m_images;
	}

	[[nodiscard]]
	auto pendingBuffers() const -> std::span<const vk::BufferMemoryBarrier2> {
		return m_buffers;
	}

	[[nodiscard]]
	auto pendingMemory() const -> std::span<const vk::MemoryBarrier2> {
		return m_memory;
	}

private:
	std::vector<vk::ImageMemoryBarrier2> m_images;
	std::vector<vk::BufferMemoryBarrier2> m_buffers;
	std::vector<vk::MemoryBarrier2> m_memory;
};

TOAST_API void
    transition(vk::CommandBuffer cmd, vk::Image image, const vk::ImageSubresourceRange& range, Usage previous, Usage next);

TOAST_API void transition(vk::CommandBuffer cmd, ImageState& state, Usage next);

struct SemaphoreWait {
	vk::Semaphore semaphore;
	vk::PipelineStageFlags2 stages;
};

/// Signals fire once every command in the submit completes
struct SubmitDesc {
	std::span<const vk::CommandBuffer> commands;
	std::span<const SemaphoreWait> waits;
	std::span<const vk::Semaphore> signals;
	vk::Fence fence;
	/// Chained to SubmitInfo2 such as a FrameBoundaryEXT
	const void* next = nullptr;
};

/// The caller holds any lock the queue needs
TOAST_API void submit(vk::Queue queue, const SubmitDesc& desc);

}
