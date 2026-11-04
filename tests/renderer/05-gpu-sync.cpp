#include "test_registry.hpp"

#include <cassert>
#include <toast/renderer/core/gpu_sync.hpp>

// No device needed since batches are inspected before flush
TOAST_TEST_NAMED("Renderer", "renderer/05-gpu-sync", test_renderer_05_gpu_sync) {
	using renderer::sync::BarrierBatch;
	using renderer::sync::ImageState;
	using renderer::sync::info;
	using renderer::sync::Usage;
	using renderer::sync::writes;
	using Stage = vk::PipelineStageFlagBits2;
	using Access = vk::AccessFlagBits2;

	const vk::ImageSubresourceRange color_range {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
	// Any non null handle since nothing is recorded
	const auto fake_image = reinterpret_cast<VkImage>(uintptr_t {0x1000});
	const auto fake_buffer = reinterpret_cast<VkBuffer>(uintptr_t {0x2000});

	for (const Usage usage : {Usage::color_attachment, Usage::depth_attachment, Usage::transfer_dst, Usage::compute_storage_write,
	                          Usage::host_write, Usage::acceleration_build, Usage::general}) {
		assert(writes(usage));
	}
	for (const Usage usage :
	     {Usage::none, Usage::depth_sampled, Usage::fragment_sampled, Usage::compute_sampled, Usage::shader_sampled,
	      Usage::compute_storage_read, Usage::vertex_input, Usage::indirect_read, Usage::transfer_src, Usage::host_read,
	      Usage::present, Usage::acceleration_trace_fragment}) {
		assert(!writes(usage));
	}

	assert(info(Usage::color_attachment).layout == vk::ImageLayout::eColorAttachmentOptimal);
	assert(info(Usage::depth_attachment).layout == vk::ImageLayout::eDepthAttachmentOptimal);
	assert(info(Usage::depth_sampled).layout == vk::ImageLayout::eDepthReadOnlyOptimal);
	assert(info(Usage::shader_sampled).layout == vk::ImageLayout::eShaderReadOnlyOptimal);
	assert(info(Usage::present).layout == vk::ImageLayout::ePresentSrcKHR);
	// The first swapchain use chains onto the acquire semaphore wait at this stage
	assert(info(Usage::present).stages == Stage::eColorAttachmentOutput);

	{
		BarrierBatch batch;
		batch.image(fake_image, color_range, Usage::shader_sampled, Usage::fragment_sampled);
		batch.image(fake_image, color_range, Usage::shader_sampled, Usage::compute_sampled);
		batch.image(fake_image, color_range, Usage::fragment_sampled, Usage::fragment_sampled);
		assert(batch.empty());
	}

	// The write before was only made visible to the earlier read stage
	{
		BarrierBatch batch;
		batch.image(fake_image, color_range, Usage::fragment_sampled, Usage::compute_sampled);
		assert(batch.pendingImages().size() == 1);
		const auto& barrier = batch.pendingImages()[0];
		assert(barrier.oldLayout == barrier.newLayout);
		assert(barrier.srcStageMask == Stage::eFragmentShader);
		assert(barrier.srcAccessMask == vk::AccessFlags2 {});
		assert(barrier.dstStageMask == Stage::eComputeShader);
		assert(barrier.dstAccessMask == Access::eShaderSampledRead);
	}

	// Read to read in another layout needs an execution dependency only
	{
		BarrierBatch batch;
		batch.image(fake_image, color_range, Usage::fragment_sampled, Usage::transfer_src);
		assert(batch.pendingImages().size() == 1);
		const auto& barrier = batch.pendingImages()[0];
		assert(barrier.oldLayout == vk::ImageLayout::eShaderReadOnlyOptimal);
		assert(barrier.newLayout == vk::ImageLayout::eTransferSrcOptimal);
		assert(barrier.srcStageMask == Stage::eFragmentShader);
		assert(barrier.srcAccessMask == vk::AccessFlags2 {});
		assert(barrier.dstAccessMask == Access::eTransferRead);
	}

	// Two scopes writing one attachment are ordered even without a layout change
	{
		BarrierBatch batch;
		batch.image(fake_image, color_range, Usage::color_attachment, Usage::color_attachment);
		assert(batch.pendingImages().size() == 1);
		const auto& barrier = batch.pendingImages()[0];
		assert(barrier.oldLayout == barrier.newLayout);
		assert(barrier.srcAccessMask == Access::eColorAttachmentWrite);
		assert(barrier.dstAccessMask == (Access::eColorAttachmentRead | Access::eColorAttachmentWrite));
	}

	// The source scope keeps the depth write and drops the test read
	{
		BarrierBatch batch;
		batch.image(fake_image, color_range, Usage::depth_attachment, Usage::depth_sampled);
		const auto& barrier = batch.pendingImages()[0];
		assert(barrier.srcAccessMask == Access::eDepthStencilAttachmentWrite);
		assert(barrier.srcStageMask == (Stage::eEarlyFragmentTests | Stage::eLateFragmentTests));
		assert(barrier.dstStageMask == (Stage::eFragmentShader | Stage::eComputeShader));
	}

	{
		BarrierBatch batch;
		batch.image(fake_image, color_range, Usage::none, Usage::color_attachment);
		const auto& barrier = batch.pendingImages()[0];
		assert(barrier.oldLayout == vk::ImageLayout::eUndefined);
		assert(barrier.srcStageMask == Stage::eNone);
		assert(barrier.srcAccessMask == vk::AccessFlags2 {});
	}

	{
		BarrierBatch batch;
		batch.image(fake_image, color_range, Usage::color_attachment, Usage::none);
		assert(batch.empty());
	}

	// A missing image is ignored rather than recorded
	{
		BarrierBatch batch;
		ImageState state;
		batch.image(state, Usage::color_attachment);
		assert(batch.empty());
		assert(state.usage == Usage::none);

		state.reset(fake_image, color_range);
		batch.image(state, Usage::color_attachment);
		batch.image(state, Usage::shader_sampled);
		batch.image(state, Usage::shader_sampled);
		assert(state.usage == Usage::shader_sampled);
		assert(batch.pendingImages().size() == 2);
		assert(batch.pendingImages()[1].oldLayout == vk::ImageLayout::eColorAttachmentOptimal);

		state.reset(fake_image, color_range);
		assert(state.usage == Usage::none);
	}

	// Buffers need a barrier only around a write
	{
		BarrierBatch batch;
		batch.buffer(fake_buffer, Usage::transfer_dst, Usage::host_read);
		batch.buffer(fake_buffer, Usage::vertex_input, Usage::vertex_input);
		batch.buffer(fake_buffer, Usage::none, Usage::compute_storage_write);
		assert(batch.pendingBuffers().size() == 1);
		assert(batch.pendingBuffers()[0].srcAccessMask == Access::eTransferWrite);
		assert(batch.pendingBuffers()[0].dstStageMask == Stage::eHost);
	}

	{
		BarrierBatch batch;
		batch.memory(Usage::acceleration_build, Usage::acceleration_trace_fragment);
		batch.memory(Usage::shader_sampled, Usage::compute_sampled);
		assert(batch.pendingMemory().size() == 1);
		assert(batch.pendingMemory()[0].srcAccessMask == Access::eAccelerationStructureWriteKHR);
	}
}
