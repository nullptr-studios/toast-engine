/**
 * @file bloom_pass.cpp
 * @author dario
 * @date 03/08/2026
 */

#include "bloom_pass.hpp"

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

namespace {

constexpr uint32_t k_max_mips = 6;

constexpr uint32_t k_min_mip_size = 8;

}

BloomPass::BloomPass(const VulkanCore& core, vk::Format hdr_format, vk::Extent2D extent) : m_core(&core), m_format(hdr_format) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/bloom.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "BloomPass shader core://shaders/bloom.slang unavailable; bloom is disabled");
		setEnabled(false);
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "BloomPass");

	const auto sampler_ci = linearClampSamplerInfo();
	m_sampler = vk::raii::Sampler(core.getDevice(), sampler_ci);
	setDebugName(core, *m_sampler, "BloomPass Sampler");

	createPipelines(core);
	createTargets(core, extent);
}

void BloomPass::createPipelines(const VulkanCore& core) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/bloom.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.color_format = m_format;
	config.extent = vk::Extent2D {1, 1};
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;

	config.debug_name = "BloomPass Downsample[0]";
	config.fragment_entry = "fragmentDownsampleFirst";
	m_downsample_first_pipeline.rebuild(core, config);

	config.debug_name = "BloomPass Downsample";
	config.fragment_entry = "fragmentDownsampleNext";
	m_downsample_pipeline.rebuild(core, config);

	config.debug_name = "BloomPass Upsample";
	config.fragment_entry = "fragmentUpsample";
	config.blend_preset = VulkanPipeline::BlendPreset::additive;
	m_upsample_pipeline.rebuild(core, config);

	config.debug_name = "BloomPass Composite";
	config.fragment_entry = "fragmentComposite";
	config.blend_preset = VulkanPipeline::BlendPreset::none;
	m_composite_pipeline.rebuild(core, config);
}

void BloomPass::createTargets(const VulkanCore& core, vk::Extent2D extent) {
	ZoneScoped;
	const auto& device = core.getDevice();

	m_mips.clear();
	m_composite = {};

	const auto make_target = [&](Mip& mip, vk::Extent2D size, std::string_view debug_name) {
		mip.extent = size;
		mip.layout = vk::ImageLayout::eUndefined;

		const auto image_ci = colorTargetImageInfo(size, m_format);

		vma::AllocationCreateInfo allocation_ci {};
		allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

		mip.image.emplace(core.getAllocator().createImage(image_ci, allocation_ci));
		setDebugName(core, **mip.image, std::string(debug_name));

		vk::ImageViewCreateInfo view_ci {};
		view_ci.image = **mip.image;
		view_ci.viewType = vk::ImageViewType::e2D;
		view_ci.format = m_format;
		view_ci.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		mip.view.emplace(device, view_ci);
		setDebugName(core, **mip.view, std::format("{} View", debug_name));
	};

	vk::Extent2D size = extent;
	for (uint32_t level = 0; level < k_max_mips; ++level) {
		size = vk::Extent2D {std::max(size.width / 2, 1u), std::max(size.height / 2, 1u)};
		if (size.width < k_min_mip_size || size.height < k_min_mip_size) {
			break;
		}
		Mip mip;
		make_target(mip, size, std::format("BloomPass Mip[{}]", level));
		m_mips.push_back(std::move(mip));
	}

	make_target(m_composite, extent, "BloomPass Composite");

	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty() || m_mips.empty()) {
		return;
	}

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	const auto allocate = [&]() {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		return std::move(allocated[0]);
	};

	m_downsample_sets.clear();
	m_upsample_sets.clear();
	for (size_t i = 0; i < m_mips.size(); ++i) {
		m_downsample_sets.push_back(allocate());
	}
	for (size_t i = 0; i + 1 < m_mips.size(); ++i) {
		m_upsample_sets.push_back(allocate());
	}
	m_composite_set = allocate();

	m_bound_source = nullptr;

	TOAST_TRACE("Render", "BloomPass targets created: {} mips from {}x{}", m_mips.size(), extent.width, extent.height);
}

void BloomPass::writeDescriptor(vk::DescriptorSet set, vk::ImageView source, vk::ImageView scene) {
	std::array<vk::DescriptorImageInfo, 2> image_infos {};
	image_infos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	image_infos[0].imageView = source;
	image_infos[0].sampler = *m_sampler;

	image_infos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	image_infos[1].imageView = scene ? scene : source;
	image_infos[1].sampler = *m_sampler;

	const std::array<vk::WriteDescriptorSet, 2> writes {
	  vk::WriteDescriptorSet(set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, image_infos.data()),
	  vk::WriteDescriptorSet(set, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_infos[1]),
	};
	m_core->getDevice().updateDescriptorSets(writes, {});
}

void BloomPass::transition(
    vk::CommandBuffer cmd, Mip& mip, vk::ImageLayout new_layout, vk::AccessFlags dst_access, vk::PipelineStageFlags dst_stage
) {
	if (!mip.image.has_value() || mip.layout == new_layout) {
		return;
	}

	const bool was_attachment = mip.layout == vk::ImageLayout::eColorAttachmentOptimal;
	const vk::ImageMemoryBarrier barrier(
	    was_attachment ? vk::AccessFlagBits::eColorAttachmentWrite : vk::AccessFlags {},
	    dst_access,
	    mip.layout,
	    new_layout,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    **mip.image,
	    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
	);
	cmd.pipelineBarrier(
	    was_attachment ? vk::PipelineStageFlagBits::eColorAttachmentOutput : vk::PipelineStageFlagBits::eTopOfPipe,
	    dst_stage,
	    {},
	    nullptr,
	    nullptr,
	    barrier
	);
	mip.layout = new_layout;
}

void BloomPass::drawInto(
    vk::CommandBuffer cmd, const VulkanPipeline& pipeline, vk::DescriptorSet set, const Mip& target, const Params& params,
    bool additive
) {
	vk::RenderingAttachmentInfo attachment {};
	attachment.imageView = **target.view;
	attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	attachment.loadOp = additive ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eDontCare;
	attachment.storeOp = vk::AttachmentStoreOp::eStore;

	vk::RenderingInfo rendering_info {};
	rendering_info.renderArea = vk::Rect2D({0, 0}, target.extent);
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &attachment;

	cmd.beginRendering(rendering_info);

	const vk::Viewport viewport(
	    0.0f, 0.0f, static_cast<float>(target.extent.width), static_cast<float>(target.extent.height), 0.0f, 1.0f
	);
	cmd.setViewport(0, std::array {viewport});
	cmd.setScissor(0, std::array {vk::Rect2D({0, 0}, target.extent)});

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics, *m_shader_layout.getPipelineLayout(), 0, std::array<vk::DescriptorSet, 1> {set}, {}
	);
	cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
	cmd.draw(3, 1, 0, 0);

	cmd.endRendering();
}

auto BloomPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	(void)frame_index;

	if (m_mips.empty() || !m_composite.view.has_value() || !m_composite_pipeline.isReady() || !source_view) {
		return source_view;
	}

	if (m_bound_source != source_view) {
		writeDescriptor(*m_downsample_sets[0], source_view, source_view);
		for (size_t i = 1; i < m_mips.size(); ++i) {
			writeDescriptor(*m_downsample_sets[i], **m_mips[i - 1].view, source_view);
		}
		for (size_t i = 0; i + 1 < m_mips.size(); ++i) {
			const size_t from = m_mips.size() - 1 - i;
			writeDescriptor(*m_upsample_sets[i], **m_mips[from].view, source_view);
		}
		writeDescriptor(*m_composite_set, **m_mips[0].view, source_view);
		m_bound_source = source_view;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	const auto settings = frame != nullptr ? frame->post_process.bloom : VulkanRenderer::PostProcessSettings::Bloom {};

	Params params {};
	params.threshold = settings.threshold;
	params.knee = settings.knee;
	params.filter_radius = settings.filter_radius;
	params.strength = settings.strength;

	for (size_t i = 0; i < m_mips.size(); ++i) {
		const vk::Extent2D source_size =
		    i == 0 ? vk::Extent2D {m_mips[0].extent.width * 2, m_mips[0].extent.height * 2} : m_mips[i - 1].extent;
		params.inverse_source_size =
		    glm::vec2(1.0f / static_cast<float>(source_size.width), 1.0f / static_cast<float>(source_size.height));

		transition(
		    cmd,
		    m_mips[i],
		    vk::ImageLayout::eColorAttachmentOptimal,
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput
		);

		drawInto(cmd, i == 0 ? m_downsample_first_pipeline : m_downsample_pipeline, *m_downsample_sets[i], m_mips[i], params, false);

		transition(
		    cmd,
		    m_mips[i],
		    vk::ImageLayout::eShaderReadOnlyOptimal,
		    vk::AccessFlagBits::eShaderRead,
		    vk::PipelineStageFlagBits::eFragmentShader
		);
	}

	for (size_t i = 0; i + 1 < m_mips.size(); ++i) {
		const size_t from = m_mips.size() - 1 - i;
		const size_t to = from - 1;

		params.inverse_source_size =
		    glm::vec2(1.0f / static_cast<float>(m_mips[from].extent.width), 1.0f / static_cast<float>(m_mips[from].extent.height));

		transition(
		    cmd,
		    m_mips[to],
		    vk::ImageLayout::eColorAttachmentOptimal,
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput
		);

		drawInto(cmd, m_upsample_pipeline, *m_upsample_sets[i], m_mips[to], params, true);

		transition(
		    cmd,
		    m_mips[to],
		    vk::ImageLayout::eShaderReadOnlyOptimal,
		    vk::AccessFlagBits::eShaderRead,
		    vk::PipelineStageFlagBits::eFragmentShader
		);
	}

	transition(
	    cmd,
	    m_composite,
	    vk::ImageLayout::eColorAttachmentOptimal,
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    vk::PipelineStageFlagBits::eColorAttachmentOutput
	);
	drawInto(cmd, m_composite_pipeline, *m_composite_set, m_composite, params, false);
	transition(
	    cmd,
	    m_composite,
	    vk::ImageLayout::eShaderReadOnlyOptimal,
	    vk::AccessFlagBits::eShaderRead,
	    vk::PipelineStageFlagBits::eFragmentShader
	);

	return **m_composite.view;
}

void BloomPass::onResize(vk::Extent2D extent) {
	if (m_core == nullptr) {
		return;
	}
	createTargets(*m_core, extent);
}

}
