/**
 * @file tonemap_pass.cpp
 * @author dario
 * @date 03/08/2026
 */

#include "tonemap_pass.hpp"

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

	createResources(core);
	createTarget(core, extent);
	TOAST_INFO("Render", "TonemapPass ready ({})", vk::to_string(ldr_format));
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
	}
}

void TonemapPass::createTarget(const VulkanCore& core, vk::Extent2D extent) {
	m_target.create(core, extent, m_ldr_format, "TonemapPass");
	// A new view can reuse the handle value of a destroyed view
	std::ranges::fill(m_bound_views, vk::ImageView {});
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

	const Params params {
	  .exposure = settings.exposure,
	  .mode = settings.mode,
	  .gamma = settings.gamma,
	  .contrast = settings.contrast,
	  .saturation = settings.saturation,
	  .vignette = settings.vignette,
	  .grain = settings.grain,
	  .time = frame != nullptr ? frame->frame_data.time : 0.0f,
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
