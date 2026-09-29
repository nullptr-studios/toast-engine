/**
 * @file tonemap_pass.cpp
 * @author dario
 * @date 03/08/2026
 */

#include "tonemap_pass.hpp"

#include "../descriptor_writer.hpp"
#include "../shader_cache.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

/// Must match exposure.slang kBins and histogramMain numthreads
constexpr uint32_t k_histogram_bins = 256;
constexpr uint32_t k_histogram_group = 16;

void memoryBarrier(
    vk::CommandBuffer cmd, vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access, vk::PipelineStageFlags2 dst_stage,
    vk::AccessFlags2 dst_access
) {
	vk::MemoryBarrier2 barrier {};
	barrier.srcStageMask = src_stage;
	barrier.srcAccessMask = src_access;
	barrier.dstStageMask = dst_stage;
	barrier.dstAccessMask = dst_access;

	vk::DependencyInfo dependency {};
	dependency.memoryBarrierCount = 1;
	dependency.pMemoryBarriers = &barrier;
	cmd.pipelineBarrier2(dependency);
}

}

TonemapPass::TonemapPass(const VulkanCore& core, vk::Format ldr_format, vk::Extent2D extent)
    : m_core(&core),
      m_ldr_format(ldr_format) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/tonemap.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "TonemapPass shader core://shaders/tonemap.slang unavailable; the screen will stay black");
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "TonemapPass");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "TonemapPass";
	config.color_format = ldr_format;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;
	m_pipeline.rebuild(core, config);

	createExposureResources(core);
	createResources(core);
	createTarget(core, extent);
	TOAST_INFO("Render", "TonemapPass ready ({})", vk::to_string(ldr_format));
}

void TonemapPass::createExposureResources(const VulkanCore& core) {
	ZoneScoped;
	const auto create_buffer = [&core](vk::DeviceSize size, std::string_view name) {
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = size;
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
		buffer_ci.sharingMode = vk::SharingMode::eExclusive;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

		vma::raii::Buffer buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
		setDebugName(core, *buffer, std::format("TonemapPass {}", name));
		return buffer;
	};
	m_histogram.emplace(create_buffer(k_histogram_bins * sizeof(uint32_t), "Histogram"));
	m_exposure.emplace(create_buffer(2 * sizeof(float), "Exposure"));

	const auto uid = assets::resolveURI("core://shaders/exposure.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "TonemapPass shader core://shaders/exposure.slang unavailable; auto exposure is disabled");
		return;
	}

	m_exposure_layout.rebuild(core, shader->reflection, "TonemapPass Exposure");
	const auto& layouts = m_exposure_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "TonemapPass exposure shader layout has no descriptor sets");
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::compute;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_exposure_layout.getPipelineLayout();
	config.debug_name = "TonemapPass Histogram";
	config.compute_entry = "histogramMain";
	m_histogram_pipeline.rebuild(core, config);
	config.debug_name = "TonemapPass Adapt";
	config.compute_entry = "adaptMain";
	m_adapt_pipeline.rebuild(core, config);

	const auto& device = core.getDevice();
	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_exposure_sets.clear();
	m_exposure_bound_views.assign(VulkanRenderer::k_frames_in_flight, vk::ImageView {});
	DescriptorWriter writer;
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_exposure_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_exposure_sets[i], std::format("TonemapPass ExposureSet[{}]", i));

		writer.buffer(*m_exposure_sets[i], 1, vk::DescriptorType::eStorageBuffer, **m_histogram)
		    .buffer(*m_exposure_sets[i], 2, vk::DescriptorType::eStorageBuffer, **m_exposure);
	}
	writer.flush(device);

	m_exposure_ready = m_histogram_pipeline.isReady() && m_adapt_pipeline.isReady();
}

void TonemapPass::createResources(const VulkanCore& core) {
	ZoneScoped;
	const auto& device = core.getDevice();
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "TonemapPass shader layout has no descriptor sets");
		return;
	}

	const auto sampler_ci = linearClampSamplerInfo();
	m_sampler = vk::raii::Sampler(device, sampler_ci);
	setDebugName(core, *m_sampler, "TonemapPass Sampler");

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_descriptor_sets.clear();
	m_params_buffers.resize(VulkanRenderer::k_frames_in_flight);
	m_bound_views.assign(VulkanRenderer::k_frames_in_flight, vk::ImageView {});

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = sizeof(Params);
		buffer_ci.usage = vk::BufferUsageFlagBits::eUniformBuffer;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		m_params_buffers[i].gpu_buffer.emplace(core.getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(core, **m_params_buffers[i].gpu_buffer, std::format("TonemapPass Params[{}]", i));

		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("TonemapPass DescriptorSet[{}]", i));

		const vk::DescriptorBufferInfo buffer_info(**m_params_buffers[i].gpu_buffer, 0, sizeof(Params));
		const vk::WriteDescriptorSet write(*m_descriptor_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_info);
		device.updateDescriptorSets(write, {});

		if (m_exposure.has_value()) {
			const vk::DescriptorBufferInfo exposure_info(**m_exposure, 0, VK_WHOLE_SIZE);
			const vk::WriteDescriptorSet exposure_write(
			    *m_descriptor_sets[i], 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &exposure_info
			);
			device.updateDescriptorSets(exposure_write, {});
		}
	}
}

void TonemapPass::createTarget(const VulkanCore& core, vk::Extent2D extent) {
	m_target.create(core, extent, m_ldr_format, "TonemapPass");
	// A new view can reuse the handle value of a destroyed view
	std::ranges::fill(m_bound_views, vk::ImageView {});
	std::ranges::fill(m_exposure_bound_views, vk::ImageView {});
}

void TonemapPass::recordExposure(
    vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view, const PostProcessSettings::Tonemap& settings
) {
	ZoneScoped;
	if (m_exposure_bound_views[frame_index] != source_view) {
		DescriptorWriter writer;
		writer.image(
		    *m_exposure_sets[frame_index],
		    0,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_sampler,
		    source_view,
		    vk::ImageLayout::eShaderReadOnlyOptimal
		);
		writer.flush(m_core->getDevice());
		m_exposure_bound_views[frame_index] = source_view;
	}

	const auto now = std::chrono::steady_clock::now();
	const float delta_time =
	    m_was_metering ? std::clamp(std::chrono::duration<float>(now - m_last_meter).count(), 0.0f, 0.25f) : 0.0f;

	const vk::Extent2D extent = m_target.extent();
	const float min_log = std::min(settings.auto_exposure_min, settings.auto_exposure_max);
	const float max_log = std::max(settings.auto_exposure_min, settings.auto_exposure_max);
	const ExposureParams params {
	  .min_log = min_log,
	  .log_range = std::max(max_log - min_log, 0.01f),
	  .delta_time = delta_time,
	  .snap = m_was_metering ? 0.0f : 1.0f,
	  .speed_up = std::max(settings.adapt_speed_up, 0.0f),
	  .speed_down = std::max(settings.adapt_speed_down, 0.0f),
	  .width = extent.width,
	  .height = extent.height,
	};

	using Stage = vk::PipelineStageFlagBits2;
	using Access = vk::AccessFlagBits2;

	// Source was a colour attachment and exposure may still be read from last frame
	memoryBarrier(
	    cmd,
	    Stage::eColorAttachmentOutput | Stage::eFragmentShader | Stage::eComputeShader,
	    Access::eColorAttachmentWrite | Access::eShaderStorageWrite,
	    Stage::eTransfer | Stage::eComputeShader,
	    Access::eTransferWrite | Access::eShaderSampledRead | Access::eShaderStorageRead | Access::eShaderStorageWrite
	);
	cmd.fillBuffer(**m_histogram, 0, VK_WHOLE_SIZE, 0);
	memoryBarrier(
	    cmd,
	    Stage::eTransfer,
	    Access::eTransferWrite,
	    Stage::eComputeShader,
	    Access::eShaderStorageRead | Access::eShaderStorageWrite
	);

	const vk::PipelineLayout layout = *m_exposure_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_histogram_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eCompute, layout, 0, std::array<vk::DescriptorSet, 1> {*m_exposure_sets[frame_index]}, {}
	);
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(ExposureParams), &params);
	cmd.dispatch(
	    (extent.width + k_histogram_group - 1) / k_histogram_group, (extent.height + k_histogram_group - 1) / k_histogram_group, 1
	);

	memoryBarrier(
	    cmd,
	    Stage::eComputeShader,
	    Access::eShaderStorageWrite,
	    Stage::eComputeShader,
	    Access::eShaderStorageRead | Access::eShaderStorageWrite
	);
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_adapt_pipeline.getPipeline());
	cmd.dispatch(1, 1, 1);
	memoryBarrier(cmd, Stage::eComputeShader, Access::eShaderStorageWrite, Stage::eFragmentShader, Access::eShaderStorageRead);

	m_was_metering = true;
	m_last_meter = now;
}

void TonemapPass::onResize(vk::Extent2D extent) {
	if (m_core != nullptr) {
		createTarget(*m_core, extent);
	}
}

auto TonemapPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	if (!m_pipeline.isReady() || frame_index >= m_descriptor_sets.size() || !source_view || !m_target.isReady()) {
		return source_view;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	const auto settings = frame != nullptr ? frame->post_process.tonemap : VulkanRenderer::PostProcessSettings::Tonemap {};

	// Debug views keep the plain exposure so their colours stay readable
	const bool metering = settings.auto_exposure && m_exposure_ready && frame != nullptr && frame->render_mode == 0 &&
	                      frame_index < m_exposure_sets.size();
	if (metering) {
		recordExposure(cmd, frame_index, source_view, settings);
	} else {
		m_was_metering = false;
	}

	const Params params {
	  .exposure = settings.exposure,
	  .mode = settings.mode,
	  .gamma = settings.gamma,
	  .contrast = settings.contrast,
	  .saturation = settings.saturation,
	  .vignette = settings.vignette,
	  .grain = settings.grain,
	  .time = frame != nullptr ? frame->frame_data.time : 0.0f,
	  .auto_exposure = metering ? 1u : 0u,
	};

	if (m_params_buffers[frame_index].gpu_buffer.has_value()) {
		const auto& allocation = m_params_buffers[frame_index].gpu_buffer->getAllocation();
		if (auto* mapped = allocation.getInfo().pMappedData) {
			std::memcpy(mapped, &params, sizeof(Params));
			allocation.flush(0, sizeof(Params));
		}
	}

	if (m_bound_views[frame_index] != source_view) {
		vk::DescriptorImageInfo image_info {};
		image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		image_info.imageView = source_view;
		image_info.sampler = *m_sampler;

		const vk::WriteDescriptorSet write(
		    *m_descriptor_sets[frame_index], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info
		);
		m_core->getDevice().updateDescriptorSets(write, {});
		m_bound_views[frame_index] = source_view;
	}

	m_target.beginScope(cmd);

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *m_shader_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_descriptor_sets[frame_index]},
	    {}
	);
	cmd.draw(3, 1, 0, 0);

	m_target.endScope(cmd);

	return m_target.view();
}

}
