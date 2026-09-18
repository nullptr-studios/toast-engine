/**
 * @file fxaa_pass.cpp
 * @author dario
 * @date 03/08/2026
 */

#include "fxaa_pass.hpp"

#include "../shader_cache.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

FxaaPass::FxaaPass(const VulkanCore& core, vk::Format ldr_format, vk::Extent2D extent) : m_core(&core), m_ldr_format(ldr_format) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/fxaa.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "FxaaPass shader core://shaders/fxaa.slang unavailable; anti-aliasing is disabled");
		setEnabled(false);
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "FxaaPass");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "FxaaPass";
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
	TOAST_INFO("Render", "FxaaPass ready ({})", vk::to_string(ldr_format));
}

void FxaaPass::createResources(const VulkanCore& core) {
	ZoneScoped;
	const auto& device = core.getDevice();
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "FxaaPass shader layout has no descriptor sets");
		return;
	}

	const auto sampler_ci = linearClampSamplerInfo();
	m_sampler = vk::raii::Sampler(device, sampler_ci);
	setDebugName(core, *m_sampler, "FxaaPass Sampler");

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_descriptor_sets.clear();
	m_bound_views.assign(VulkanRenderer::k_frames_in_flight, vk::ImageView {});
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("FxaaPass DescriptorSet[{}]", i));
	}
}

void FxaaPass::createTarget(const VulkanCore& core, vk::Extent2D extent) {
	m_target.create(core, extent, m_ldr_format, "FxaaPass");
	std::ranges::fill(m_bound_views, vk::ImageView {});
}

void FxaaPass::onResize(vk::Extent2D extent) {
	if (m_core != nullptr) {
		createTarget(*m_core, extent);
	}
}

auto FxaaPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	if (!m_pipeline.isReady() || frame_index >= m_descriptor_sets.size() || !source_view || !m_target.isReady()) {
		return source_view;
	}

	if (m_bound_views[frame_index] != source_view) {
		vk::DescriptorImageInfo image_info {};
		image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		image_info.imageView = source_view;
		image_info.sampler = *m_sampler;

		const vk::WriteDescriptorSet write(
		    *m_descriptor_sets[frame_index], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info
		);
		m_core->getDevice().updateDescriptorSets(write, {});
		m_bound_views[frame_index] = source_view;
	}

	m_target.beginScope(cmd);

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	const auto settings = frame != nullptr ? frame->post_process.fxaa : VulkanRenderer::PostProcessSettings::Fxaa {};

	const Params params {
	  .inverse_source_size =
	      glm::vec2(1.0f / static_cast<float>(m_target.extent().width), 1.0f / static_cast<float>(m_target.extent().height)),
	  .contrast_threshold = settings.contrast_threshold,
	  .relative_threshold = settings.relative_threshold,
	  .subpixel_blending = settings.subpixel_blending,
	};

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *m_shader_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_descriptor_sets[frame_index]},
	    {}
	);
	cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
	cmd.draw(3, 1, 0, 0);

	m_target.endScope(cmd);

	return m_target.view();
}

}
