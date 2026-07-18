#include <toast/renderer/shader_compiler.hpp>
#include "rmlui_renderer_vk.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Math.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kBlurSize = 7;
constexpr int kBlurNumWeights = (kBlurSize + 1) / 2;
constexpr int kMaxNumStops = 16;

void SigmaToParameters(const float desired_sigma, int& out_pass_level, float& out_sigma) {
	constexpr int max_num_passes = 10;
	constexpr float max_single_pass_sigma = 3.0f;
	out_pass_level = Rml::Math::Clamp(Rml::Math::Log2(int(desired_sigma * (2.f / max_single_pass_sigma))), 0, max_num_passes);
	out_sigma = Rml::Math::Clamp(desired_sigma / float(1 << out_pass_level), 0.0f, max_single_pass_sigma);
}

void ComputeBlurWeights(float sigma, float out_weights[kBlurNumWeights]) {
	float normalization = 0.0f;
	for (int i = 0; i < kBlurNumWeights; i++) {
		if (Rml::Math::Absolute(sigma) < 0.1f) {
			out_weights[i] = float(i == 0);
		} else {
			out_weights[i] =
			    Rml::Math::Exp(-float(i * i) / (2.0f * sigma * sigma)) / (Rml::Math::SquareRoot(2.f * Rml::Math::RMLUI_PI) * sigma);
		}

		normalization += (i == 0 ? 1.f : 2.0f) * out_weights[i];
	}
	for (int i = 0; i < kBlurNumWeights; i++) {
		out_weights[i] /= normalization;
	}
}

// Half-texel inset so bilinear lookups clamp to fragment centers
void SetTexCoordLimits(RenderInterface_VK::effects_push_t& push, VkRect2D rectangle, VkExtent2D framebuffer_size) {
	push.m_v[2][0] = (float(rectangle.offset.x) + 0.5f) / float(framebuffer_size.width);
	push.m_v[2][1] = (float(rectangle.offset.y) + 0.5f) / float(framebuffer_size.height);
	push.m_v[2][2] = (float(rectangle.offset.x + int(rectangle.extent.width)) - 0.5f) / float(framebuffer_size.width);
	push.m_v[2][3] = (float(rectangle.offset.y + int(rectangle.extent.height)) - 0.5f) / float(framebuffer_size.height);
}

void SetIdentityUv(RenderInterface_VK::effects_push_t& push) {
	push.m_v[0][0] = 0.f;
	push.m_v[0][1] = 0.f;
	push.m_v[0][2] = 1.f;
	push.m_v[0][3] = 1.f;
}

VkViewport FullViewport(VkExtent2D extent) {
	return VkViewport {0.f, 0.f, float(extent.width), float(extent.height), 0.f, 1.f};
}

VkRect2D FullRect(VkExtent2D extent) {
	return VkRect2D {
	  {0, 0},
    extent
	};
}

}

void RenderInterface_VK::CreateEffectResources() noexcept {
	// HACK: Need to use shader compiler
	auto compiled = renderer::ShaderCompiler::compileShaderModuleFromSource("./ui_effects.slang");
	RMLUI_VK_ASSERTMSG(!compiled.spirv.empty(), "failed to compile ui_effects.slang");

	{
		VkShaderModuleCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		info.pCode = reinterpret_cast<const uint32_t*>(compiled.spirv.data());
		info.codeSize = compiled.spirv.size();

		VkResult status = vkCreateShaderModule(m_p_device, &info, nullptr, &m_p_effects_shader_module);
		RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vkCreateShaderModule for ui_effects");
	}

	// Clamp for postprocess sampling
	{
		VkSamplerCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		vkCreateSampler(m_p_device, &info, nullptr, &m_p_sampler_clamp);
	}

	// effect (set 0 = source, set 1 = mask)
	{
		VkDescriptorSetLayoutBinding binding = {};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.pBindings = &binding;
		info.bindingCount = 1;

		VkResult status = vkCreateDescriptorSetLayout(m_p_device, &info, nullptr, &m_p_layout_effect_texture);
		RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vkCreateDescriptorSetLayout (effects)");
	}

	{
		VkPushConstantRange push = {};
		push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		push.offset = 0;
		push.size = sizeof(effects_push_t);
		static_assert(sizeof(effects_push_t) == 96, "effects push block must stay within the 128-byte minimum");

		VkDescriptorSetLayout p_layouts[] = {m_p_layout_effect_texture, m_p_layout_effect_texture};

		VkPipelineLayoutCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		info.pSetLayouts = p_layouts;
		info.setLayoutCount = 2;
		info.pPushConstantRanges = &push;
		info.pushConstantRangeCount = 1;

		VkResult status = vkCreatePipelineLayout(m_p_device, &info, nullptr, &m_p_pipeline_layout_effects);
		RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vkCreatePipelineLayout (effects)");
	}

	// gradient (set 1, binding 2, dynamic UBO)
	{
		VkDescriptorSetLayoutBinding binding = {};
		binding.binding = 2;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.pBindings = &binding;
		info.bindingCount = 1;

		VkResult status = vkCreateDescriptorSetLayout(m_p_device, &info, nullptr, &m_p_layout_gradient_data);
		RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vkCreateDescriptorSetLayout (gradient)");

		VkDescriptorSetLayout p_layouts[] = {m_p_descriptor_set_layout_vertex_transform, m_p_layout_gradient_data};

		VkPipelineLayoutCreateInfo info_layout = {};
		info_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		info_layout.pSetLayouts = p_layouts;
		info_layout.setLayoutCount = 2;

		status = vkCreatePipelineLayout(m_p_device, &info_layout, nullptr, &m_p_pipeline_layout_gradient);
		RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vkCreatePipelineLayout (gradient)");

		m_manager_descriptors.Alloc_Descriptor(m_p_device, &m_p_layout_gradient_data, &m_p_descriptor_set_gradient);
		m_memory_pool.SetDescriptorSet(
		    2, sizeof(gradient_data_std140_t), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, m_p_descriptor_set_gradient
		);
	}

	const VkFormat depth_format =
	    m_depth_stencil_attachment_format == VK_FORMAT_S8_UINT ? VK_FORMAT_UNDEFINED : m_depth_stencil_attachment_format;

	// Shared state for every fullscreen effect pipeline
	auto create_fullscreen_pipeline = [&](const char* fragment_entry, bool premultiplied_blend) -> VkPipeline {
		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = m_p_effects_shader_module;
		stages[0].pName = "vsFullscreen";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = m_p_effects_shader_module;
		stages[1].pName = fragment_entry;

		VkPipelineRenderingCreateInfo info_rendering = {};
		info_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		info_rendering.colorAttachmentCount = 1;
		info_rendering.pColorAttachmentFormats = &m_color_attachment_format;

		VkPipelineVertexInputStateCreateInfo info_vertex = {};
		info_vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		VkPipelineInputAssemblyStateCreateInfo info_assembly = {};
		info_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		info_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineRasterizationStateCreateInfo info_raster = {};
		info_raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		info_raster.polygonMode = VK_POLYGON_MODE_FILL;
		info_raster.cullMode = VK_CULL_MODE_NONE;
		info_raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		info_raster.lineWidth = 1.0f;

		VkPipelineColorBlendAttachmentState info_blend_att = {};
		info_blend_att.colorWriteMask = 0xf;
		info_blend_att.blendEnable = premultiplied_blend ? VK_TRUE : VK_FALSE;
		info_blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		info_blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		info_blend_att.colorBlendOp = VK_BLEND_OP_ADD;
		info_blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		info_blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		info_blend_att.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo info_blend = {};
		info_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		info_blend.attachmentCount = 1;
		info_blend.pAttachments = &info_blend_att;

		VkPipelineViewportStateCreateInfo info_viewport = {};
		info_viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		info_viewport.viewportCount = 1;
		info_viewport.scissorCount = 1;

		VkPipelineMultisampleStateCreateInfo info_multisample = {};
		info_multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		info_multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo info_dynamic = {};
		info_dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		info_dynamic.pDynamicStates = dynamic_states;
		info_dynamic.dynamicStateCount = 2;

		VkGraphicsPipelineCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.pNext = &info_rendering;
		info.stageCount = 2;
		info.pStages = stages;
		info.pVertexInputState = &info_vertex;
		info.pInputAssemblyState = &info_assembly;
		info.pViewportState = &info_viewport;
		info.pRasterizationState = &info_raster;
		info.pMultisampleState = &info_multisample;
		info.pColorBlendState = &info_blend;
		info.pDynamicState = &info_dynamic;
		info.layout = m_p_pipeline_layout_effects;

		VkPipeline p_pipeline = nullptr;
		VkResult status = vkCreateGraphicsPipelines(m_p_device, nullptr, 1, &info, nullptr, &p_pipeline);
		RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vkCreateGraphicsPipelines (effects)");
		return p_pipeline;
	};

	m_p_pipeline_passthrough_blend = create_fullscreen_pipeline("fsPassthrough", true);
	m_p_pipeline_passthrough_noblend = create_fullscreen_pipeline("fsPassthrough", false);
	m_p_pipeline_colormatrix = create_fullscreen_pipeline("fsColorMatrix", false);
	m_p_pipeline_blendmask = create_fullscreen_pipeline("fsBlendMask", false);
	m_p_pipeline_blur = create_fullscreen_pipeline("fsBlur", false);
	m_p_pipeline_dropshadow = create_fullscreen_pipeline("fsDropShadow", false);

	// Gradient pipelines
	auto create_gradient_pipeline = [&](bool stencil_test) -> VkPipeline {
		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = m_p_effects_shader_module;
		stages[0].pName = "vsGeometry";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = m_p_effects_shader_module;
		stages[1].pName = "fsGradient";

		VkPipelineRenderingCreateInfo info_rendering = {};
		info_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		info_rendering.colorAttachmentCount = 1;
		info_rendering.pColorAttachmentFormats = &m_color_attachment_format;
		info_rendering.depthAttachmentFormat = depth_format;
		info_rendering.stencilAttachmentFormat = m_depth_stencil_attachment_format;

		VkVertexInputBindingDescription info_binding = {};
		info_binding.binding = 0;
		info_binding.stride = sizeof(Rml::Vertex);
		info_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attributes[3] = {};
		attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Rml::Vertex, position)};
		attributes[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Rml::Vertex, colour)};
		attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Rml::Vertex, tex_coord)};

		VkPipelineVertexInputStateCreateInfo info_vertex = {};
		info_vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		info_vertex.pVertexBindingDescriptions = &info_binding;
		info_vertex.vertexBindingDescriptionCount = 1;
		info_vertex.pVertexAttributeDescriptions = attributes;
		info_vertex.vertexAttributeDescriptionCount = 3;

		VkPipelineInputAssemblyStateCreateInfo info_assembly = {};
		info_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		info_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineRasterizationStateCreateInfo info_raster = {};
		info_raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		info_raster.polygonMode = VK_POLYGON_MODE_FILL;
		info_raster.cullMode = VK_CULL_MODE_NONE;
		info_raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		info_raster.lineWidth = 1.0f;

		VkPipelineColorBlendAttachmentState info_blend_att = {};
		info_blend_att.colorWriteMask = 0xf;
		info_blend_att.blendEnable = VK_TRUE;
		info_blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		info_blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		info_blend_att.colorBlendOp = VK_BLEND_OP_ADD;
		info_blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		info_blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		info_blend_att.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo info_blend = {};
		info_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		info_blend.attachmentCount = 1;
		info_blend.pAttachments = &info_blend_att;

		VkPipelineDepthStencilStateCreateInfo info_depth = {};
		info_depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		info_depth.depthTestEnable = VK_FALSE;
		info_depth.depthWriteEnable = VK_FALSE;
		info_depth.stencilTestEnable = stencil_test ? VK_TRUE : VK_FALSE;
		info_depth.back.compareOp = VK_COMPARE_OP_EQUAL;
		info_depth.back.failOp = VK_STENCIL_OP_KEEP;
		info_depth.back.depthFailOp = VK_STENCIL_OP_KEEP;
		info_depth.back.passOp = VK_STENCIL_OP_KEEP;
		info_depth.back.compareMask = ~0u;
		info_depth.back.writeMask = 0;
		info_depth.front = info_depth.back;

		VkPipelineViewportStateCreateInfo info_viewport = {};
		info_viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		info_viewport.viewportCount = 1;
		info_viewport.scissorCount = 1;

		VkPipelineMultisampleStateCreateInfo info_multisample = {};
		info_multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		info_multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
		VkPipelineDynamicStateCreateInfo info_dynamic = {};
		info_dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		info_dynamic.pDynamicStates = dynamic_states;
		info_dynamic.dynamicStateCount = 3;

		VkGraphicsPipelineCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.pNext = &info_rendering;
		info.stageCount = 2;
		info.pStages = stages;
		info.pVertexInputState = &info_vertex;
		info.pInputAssemblyState = &info_assembly;
		info.pViewportState = &info_viewport;
		info.pRasterizationState = &info_raster;
		info.pMultisampleState = &info_multisample;
		info.pColorBlendState = &info_blend;
		info.pDepthStencilState = &info_depth;
		info.pDynamicState = &info_dynamic;
		info.layout = m_p_pipeline_layout_gradient;

		VkPipeline p_pipeline = nullptr;
		VkResult status = vkCreateGraphicsPipelines(m_p_device, nullptr, 1, &info, nullptr, &p_pipeline);
		RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vkCreateGraphicsPipelines (gradient)");
		return p_pipeline;
	};

	m_p_pipeline_gradient = create_gradient_pipeline(false);
	m_p_pipeline_gradient_stencil = create_gradient_pipeline(true);
}

void RenderInterface_VK::DestroyEffectResources() noexcept {
	for (auto& pool : m_pools) {
		DestroyPool(*pool);
	}
	m_pools.clear();
	for (auto& pool : m_retired_pools) {
		DestroyPool(*pool);
	}
	m_retired_pools.clear();
	m_p_current_pool = nullptr;

	for (CompiledShader* p_shader : m_pending_for_deletion_shaders) {
		m_memory_pool.Free_Allocation(p_shader->m_allocation);
		delete p_shader;
	}
	m_pending_for_deletion_shaders.clear();

	vkDestroyPipeline(m_p_device, m_p_pipeline_passthrough_blend, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_passthrough_noblend, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_colormatrix, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_blendmask, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_blur, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_dropshadow, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_gradient, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_gradient_stencil, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_clip_write_incr, nullptr);

	vkDestroyPipelineLayout(m_p_device, m_p_pipeline_layout_effects, nullptr);
	vkDestroyPipelineLayout(m_p_device, m_p_pipeline_layout_gradient, nullptr);

	if (m_p_descriptor_set_gradient) {
		m_manager_descriptors.Free_Descriptors(m_p_device, &m_p_descriptor_set_gradient);
	}

	vkDestroyDescriptorSetLayout(m_p_device, m_p_layout_effect_texture, nullptr);
	vkDestroyDescriptorSetLayout(m_p_device, m_p_layout_gradient_data, nullptr);
	vkDestroySampler(m_p_device, m_p_sampler_clamp, nullptr);
	vkDestroyShaderModule(m_p_device, m_p_effects_shader_module, nullptr);
}

RenderInterface_VK::effect_image_t
    RenderInterface_VK::CreateEffectImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage) noexcept {
	effect_image_t image;

	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent = {extent.width, extent.height, 1};
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usage;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo info_allocation = {};
	info_allocation.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkResult status = vmaCreateImage(m_p_allocator, &info, &info_allocation, &image.m_p_image, &image.m_p_allocation, nullptr);
	RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vmaCreateImage (effect image)");

	const bool is_depth_stencil = (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;

	VkImageViewCreateInfo info_view = {};
	info_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info_view.image = image.m_p_image;
	info_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info_view.format = format;
	info_view.subresourceRange.baseMipLevel = 0;
	info_view.subresourceRange.levelCount = 1;
	info_view.subresourceRange.baseArrayLayer = 0;
	info_view.subresourceRange.layerCount = 1;
	if (is_depth_stencil) {
		info_view.subresourceRange.aspectMask =
		    format == VK_FORMAT_S8_UINT ? VK_IMAGE_ASPECT_STENCIL_BIT : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
	} else {
		info_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	status = vkCreateImageView(m_p_device, &info_view, nullptr, &image.m_p_view);
	RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vkCreateImageView (effect image)");

	return image;
}

void RenderInterface_VK::DestroyEffectImage(effect_image_t& image) noexcept {
	if (!image.m_p_image) {
		return;
	}

	if (image.m_p_descriptor_set) {
		m_manager_descriptors.Free_Descriptors(m_p_device, &image.m_p_descriptor_set);
	}

	vkDestroyImageView(m_p_device, image.m_p_view, nullptr);
	vmaDestroyImage(m_p_allocator, image.m_p_image, image.m_p_allocation);
	image = {};
}

RenderInterface_VK::LayerPool& RenderInterface_VK::AcquirePool(VkExtent2D extent) {
	for (auto& pool : m_pools) {
		if (pool->m_extent.width == extent.width && pool->m_extent.height == extent.height) {
			return *pool;
		}
	}

	auto pool = Rml::MakeUnique<LayerPool>();
	pool->m_generation = ++m_pool_generation;
	pool->m_extent = extent;
	pool->m_stencil = CreateEffectImage(extent, m_depth_stencil_attachment_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

	m_pools.push_back(std::move(pool));
	return *m_pools.back();
}

void RenderInterface_VK::DestroyPool(LayerPool& pool) noexcept {
	for (auto& layer : pool.m_layers) {
		DestroyEffectImage(layer);
	}
	for (auto& output : pool.m_outputs) {
		DestroyEffectImage(output);
	}
	for (auto& postprocess : pool.m_postprocess) {
		DestroyEffectImage(postprocess);
	}
	DestroyEffectImage(pool.m_blend_mask);
	DestroyEffectImage(pool.m_stencil);
}

void RenderInterface_VK::RetireUnusedPools() noexcept {
	for (auto it = m_retired_pools.begin(); it != m_retired_pools.end();) {
		if (!m_command_buffer_ring.IsGenerationReferenced((*it)->m_generation)) {
			DestroyPool(**it);
			it = m_retired_pools.erase(it);
		} else {
			++it;
		}
	}
}

VkDescriptorSet RenderInterface_VK::GetEffectDescriptorSet(effect_image_t& image) noexcept {
	if (image.m_p_descriptor_set) {
		return image.m_p_descriptor_set;
	}

	m_manager_descriptors.Alloc_Descriptor(m_p_device, &m_p_layout_effect_texture, &image.m_p_descriptor_set);

	VkDescriptorImageInfo info_image = {};
	info_image.imageView = image.m_p_view;
	info_image.sampler = m_p_sampler_clamp;
	info_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet info_write = {};
	info_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	info_write.dstSet = image.m_p_descriptor_set;
	info_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	info_write.dstBinding = 0;
	info_write.pImageInfo = &info_image;
	info_write.descriptorCount = 1;

	vkUpdateDescriptorSets(m_p_device, 1, &info_write, 0, nullptr);
	return image.m_p_descriptor_set;
}

void RenderInterface_VK::TransitionEffectImage(effect_image_t& image, VkImageLayout new_layout) {
	if (image.m_layout == new_layout) {
		return;
	}

	const bool is_stencil = image.m_p_image == m_p_current_pool->m_stencil.m_p_image;

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = image.m_layout;
	barrier.newLayout = new_layout;
	barrier.image = image.m_p_image;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.aspectMask = is_stencil ? (m_depth_stencil_attachment_format == VK_FORMAT_S8_UINT
	                                                        ? VK_IMAGE_ASPECT_STENCIL_BIT
	                                                        : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
	                                                 : VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

	// All-stage barrier
	vkCmdPipelineBarrier(
	    m_p_current_command_buffer,
	    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	    0,
	    0,
	    nullptr,
	    0,
	    nullptr,
	    1,
	    &barrier
	);

	image.m_layout = new_layout;
}

void RenderInterface_VK::BeginLayerScope(effect_image_t& target, bool clear_color, bool clear_stencil) {
	RMLUI_VK_ASSERTMSG(!m_scope_active, "a rendering scope is already open");

	LayerPool& pool = *m_p_current_pool;
	TransitionEffectImage(target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	TransitionEffectImage(pool.m_stencil, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	VkRenderingAttachmentInfo info_color = {};
	info_color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	info_color.imageView = target.m_p_view;
	info_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	info_color.loadOp = clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	info_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	info_color.clearValue.color = {
	  {0.f, 0.f, 0.f, 0.f}
	};

	VkRenderingAttachmentInfo info_stencil = {};
	info_stencil.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	info_stencil.imageView = pool.m_stencil.m_p_view;
	info_stencil.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	info_stencil.loadOp = clear_stencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	info_stencil.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	info_stencil.clearValue.depthStencil = {1.f, 0};

	VkRenderingInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	info.renderArea = FullRect(pool.m_extent);
	info.layerCount = 1;
	info.colorAttachmentCount = 1;
	info.pColorAttachments = &info_color;
	if (m_depth_stencil_attachment_format != VK_FORMAT_S8_UINT) {
		info.pDepthAttachment = &info_stencil;
	}
	info.pStencilAttachment = &info_stencil;

	vkCmdBeginRendering(m_p_current_command_buffer, &info);

	vkCmdSetViewport(m_p_current_command_buffer, 0, 1, &m_viewport);
	const VkRect2D scissor = CurrentScissor();
	vkCmdSetScissor(m_p_current_command_buffer, 0, 1, &scissor);
	vkCmdSetStencilReference(m_p_current_command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, uint32_t(m_stencil_test_value));

	m_scope_active = true;
	m_p_scope_target = &target;
}

void RenderInterface_VK::BeginEffectScope(effect_image_t& target, bool clear_color, VkRect2D render_area) {
	RMLUI_VK_ASSERTMSG(!m_scope_active, "a rendering scope is already open");

	TransitionEffectImage(target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkRenderingAttachmentInfo info_color = {};
	info_color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	info_color.imageView = target.m_p_view;
	info_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	info_color.loadOp = clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	info_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	info_color.clearValue.color = {
	  {0.f, 0.f, 0.f, 0.f}
	};

	VkRenderingInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	info.renderArea = render_area;
	info.layerCount = 1;
	info.colorAttachmentCount = 1;
	info.pColorAttachments = &info_color;

	vkCmdBeginRendering(m_p_current_command_buffer, &info);

	m_scope_active = true;
	m_p_scope_target = &target;
}

void RenderInterface_VK::EndScope() {
	RMLUI_VK_ASSERTMSG(m_scope_active, "no rendering scope to end");
	vkCmdEndRendering(m_p_current_command_buffer);
	m_scope_active = false;
	m_p_scope_target = nullptr;
}

void RenderInterface_VK::SuspendLayerScope() {
	EndScope();
}

void RenderInterface_VK::ResumeLayerScope() {
	BeginLayerScope(*m_layer_stack.back(), false, false);
}

VkRect2D RenderInterface_VK::CurrentScissor() const {
	return m_is_use_scissor_specified ? m_scissor : m_scissor_original;
}

void RenderInterface_VK::RenderFullscreenPass(
    VkPipeline pipeline, effect_image_t& destination, effect_image_t& source, effect_image_t* mask, const effects_push_t& push,
    bool clear_destination, VkRect2D render_area, VkViewport viewport
) {
	VkCommandBuffer cmd = m_p_current_command_buffer;

	TransitionEffectImage(source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	if (mask) {
		TransitionEffectImage(*mask, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	VkDescriptorSet p_sets[] = {
	  GetEffectDescriptorSet(source), mask ? GetEffectDescriptorSet(*mask) : GetEffectDescriptorSet(source)
	};

	BeginEffectScope(destination, clear_destination, FullRect(m_p_current_pool->m_extent));

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &render_area);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_p_pipeline_layout_effects, 0, 2, p_sets, 0, nullptr);
	vkCmdPushConstants(
	    cmd,
	    m_p_pipeline_layout_effects,
	    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	    0,
	    sizeof(effects_push_t),
	    &push
	);
	vkCmdDraw(cmd, 3, 1, 0, 0);

	EndScope();
}

void RenderInterface_VK::EnableClipMask(bool enable) {
	m_is_apply_to_regular_geometry_stencil = enable;
}

void RenderInterface_VK::RenderToClipMask(
    Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation
) {
	using Rml::ClipMaskOperation;

	if (m_p_current_command_buffer == nullptr) {
		return;
	}

	int stencil_test_value = m_stencil_test_value;

	switch (operation) {
		case ClipMaskOperation::Set: {
			VkClearAttachment clear = {};
			clear.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
			clear.clearValue.depthStencil = {1.f, 0};
			VkClearRect rect = {};
			rect.layerCount = 1;
			rect.rect = {
			  {			          0,			            0},
        {uint32_t(m_width), uint32_t(m_height)}
			};
			vkCmdClearAttachments(m_p_current_command_buffer, 1, &clear, 1, &rect);

			m_clip_incr = false;
			m_clip_write_value = 1;
			stencil_test_value = 1;
		} break;
		case ClipMaskOperation::SetInverse: {
			VkClearAttachment clear = {};
			clear.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
			clear.clearValue.depthStencil = {1.f, 1};
			VkClearRect rect = {};
			rect.layerCount = 1;
			rect.rect = {
			  {			          0,			            0},
        {uint32_t(m_width), uint32_t(m_height)}
			};
			vkCmdClearAttachments(m_p_current_command_buffer, 1, &clear, 1, &rect);

			m_clip_incr = false;
			m_clip_write_value = 0;
			stencil_test_value = 1;
		} break;
		case ClipMaskOperation::Intersect: {
			m_clip_incr = true;
			stencil_test_value = m_stencil_test_value + 1;
		} break;
	}

	m_is_use_stencil_pipeline = true;
	RenderGeometry(geometry, translation, {});
	m_is_use_stencil_pipeline = false;

	m_stencil_test_value = stencil_test_value;
	vkCmdSetStencilReference(m_p_current_command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, uint32_t(m_stencil_test_value));
}

Rml::LayerHandle RenderInterface_VK::PushLayer() {
	LayerPool& pool = *m_p_current_pool;

	EndScope();

	if (m_stack_layers_used == pool.m_layers.size()) {
		pool.m_layers.push_back(CreateEffectImage(
		    pool.m_extent,
		    m_color_attachment_format,
		    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		));
	}

	effect_image_t* p_layer = &pool.m_layers[m_stack_layers_used++];
	m_layer_stack.push_back(p_layer);

	BeginLayerScope(*p_layer, true, false);

	return Rml::LayerHandle(m_layer_stack.size() - 1);
}

void RenderInterface_VK::CompositeLayers(
    Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
    Rml::Span<const Rml::CompiledFilterHandle> filters
) {
	LayerPool& pool = *m_p_current_pool;
	const VkRect2D scissor = CurrentScissor();

	SuspendLayerScope();

	// Copy the source layer into the primary postprocess buffer
	{
		effects_push_t push = {};
		SetIdentityUv(push);
		push.m_v[1][0] = 1.f;
		RenderFullscreenPass(
		    m_p_pipeline_passthrough_noblend,
		    pool.m_postprocess[0],
		    *m_layer_stack[size_t(source)],
		    nullptr,
		    push,
		    false,
		    scissor,
		    FullViewport(pool.m_extent)
		);
	}

	RenderFilters(filters);

	// Draw the result into the destination layer
	{
		effects_push_t push = {};
		SetIdentityUv(push);
		push.m_v[1][0] = 1.f;
		const VkPipeline pipeline =
		    blend_mode == Rml::BlendMode::Blend ? m_p_pipeline_passthrough_blend : m_p_pipeline_passthrough_noblend;
		RenderFullscreenPass(
		    pipeline,
		    *m_layer_stack[size_t(destination)],
		    pool.m_postprocess[0],
		    nullptr,
		    push,
		    false,
		    scissor,
		    FullViewport(pool.m_extent)
		);
	}

	ResumeLayerScope();
}

void RenderInterface_VK::PopLayer() {
	RMLUI_VK_ASSERTMSG(m_layer_stack.size() > 1, "PopLayer called on the base layer");

	EndScope();
	m_layer_stack.pop_back();
	if (m_stack_layers_used > 0) {
		m_stack_layers_used--;
	}

	BeginLayerScope(*m_layer_stack.back(), false, false);
}

Rml::TextureHandle RenderInterface_VK::SaveLayerAsTexture() {
	const VkRect2D bounds = CurrentScissor();
	if (bounds.extent.width == 0 || bounds.extent.height == 0) {
		return {};
	}

	effect_image_t& top = *m_layer_stack.back();

	SuspendLayerScope();
	TransitionEffectImage(top, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	// Same image setup as CreateTexture but with a GPU copy instead of an upload
	auto* p_texture = new texture_data_t {};

	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = m_color_attachment_format;
	info.extent = {bounds.extent.width, bounds.extent.height, 1};
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo info_allocation = {};
	info_allocation.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkResult status =
	    vmaCreateImage(m_p_allocator, &info, &info_allocation, &p_texture->m_p_vk_image, &p_texture->m_p_vma_allocation, nullptr);
	RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vmaCreateImage (SaveLayerAsTexture)");

	VkImageSubresourceRange range = {};
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.levelCount = 1;
	range.layerCount = 1;

	VkImageMemoryBarrier to_dst = {};
	to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	to_dst.image = p_texture->m_p_vk_image;
	to_dst.subresourceRange = range;
	to_dst.srcAccessMask = 0;
	to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier(
	    m_p_current_command_buffer,
	    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	    VK_PIPELINE_STAGE_TRANSFER_BIT,
	    0,
	    0,
	    nullptr,
	    0,
	    nullptr,
	    1,
	    &to_dst
	);

	VkImageCopy region = {};
	region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	region.srcOffset = {bounds.offset.x, bounds.offset.y, 0};
	region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	region.extent = {bounds.extent.width, bounds.extent.height, 1};
	vkCmdCopyImage(
	    m_p_current_command_buffer,
	    top.m_p_image,
	    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	    p_texture->m_p_vk_image,
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	    1,
	    &region
	);

	VkImageMemoryBarrier to_read = {};
	to_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	to_read.image = p_texture->m_p_vk_image;
	to_read.subresourceRange = range;
	to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(
	    m_p_current_command_buffer,
	    VK_PIPELINE_STAGE_TRANSFER_BIT,
	    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	    0,
	    0,
	    nullptr,
	    0,
	    nullptr,
	    1,
	    &to_read
	);

	VkImageViewCreateInfo info_view = {};
	info_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info_view.image = p_texture->m_p_vk_image;
	info_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info_view.format = m_color_attachment_format;
	info_view.subresourceRange.levelCount = 1;
	info_view.subresourceRange.layerCount = 1;
	info_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	status = vkCreateImageView(m_p_device, &info_view, nullptr, &p_texture->m_p_vk_image_view);
	RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "failed to vkCreateImageView (SaveLayerAsTexture)");

	p_texture->m_p_vk_sampler = m_p_sampler_linear;

	ResumeLayerScope();

	return reinterpret_cast<Rml::TextureHandle>(p_texture);
}

Rml::CompiledFilterHandle RenderInterface_VK::SaveLayerAsMaskImage() {
	LayerPool& pool = *m_p_current_pool;
	effect_image_t& top = *m_layer_stack.back();

	if (!pool.m_blend_mask.m_p_image) {
		pool.m_blend_mask = CreateEffectImage(
		    pool.m_extent, m_color_attachment_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		);
	}

	SuspendLayerScope();

	effects_push_t push = {};
	SetIdentityUv(push);
	push.m_v[1][0] = 1.f;
	RenderFullscreenPass(
	    m_p_pipeline_passthrough_noblend,
	    pool.m_blend_mask,
	    top,
	    nullptr,
	    push,
	    false,
	    CurrentScissor(),
	    FullViewport(pool.m_extent)
	);

	ResumeLayerScope();

	auto* p_filter = new CompiledFilter {};
	p_filter->m_type = FilterType::MaskImage;
	return reinterpret_cast<Rml::CompiledFilterHandle>(p_filter);
}

Rml::CompiledFilterHandle RenderInterface_VK::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) {
	CompiledFilter filter = {};

	if (name == "opacity") {
		filter.m_type = FilterType::Passthrough;
		filter.m_blend_factor = Rml::Get(parameters, "value", 1.0f);
	} else if (name == "blur") {
		filter.m_type = FilterType::Blur;
		filter.m_sigma = Rml::Get(parameters, "sigma", 1.0f);
	} else if (name == "drop-shadow") {
		filter.m_type = FilterType::DropShadow;
		filter.m_sigma = Rml::Get(parameters, "sigma", 0.f);
		filter.m_color = Rml::Get(parameters, "color", Rml::Colourb()).ToPremultiplied();
		filter.m_offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));
	} else if (name == "brightness") {
		filter.m_type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		filter.m_color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
	} else if (name == "contrast") {
		filter.m_type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float grayness = 0.5f - 0.5f * value;
		filter.m_color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
		filter.m_color_matrix.SetColumn(3, Rml::Vector4f(grayness, grayness, grayness, 1.f));
	} else if (name == "invert") {
		filter.m_type = FilterType::ColorMatrix;
		const float value = Rml::Math::Clamp(Rml::Get(parameters, "value", 1.0f), 0.f, 1.f);
		const float inverted = 1.f - 2.f * value;
		filter.m_color_matrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.f);
		filter.m_color_matrix.SetColumn(3, Rml::Vector4f(value, value, value, 1.f));
	} else if (name == "grayscale") {
		filter.m_type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
		// clang-format off
		filter.m_color_matrix = Rml::Matrix4f::FromRows(
			{gray.x + rev_value, gray.y,             gray.z,             0.f},
			{gray.x,             gray.y + rev_value, gray.z,             0.f},
			{gray.x,             gray.y,             gray.z + rev_value, 0.f},
			{0.f,                0.f,                0.f,                1.f}
		);
		// clang-format on
	} else if (name == "sepia") {
		filter.m_type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f r_mix = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
		const Rml::Vector3f g_mix = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
		const Rml::Vector3f b_mix = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
		// clang-format off
		filter.m_color_matrix = Rml::Matrix4f::FromRows(
			{r_mix.x + rev_value, r_mix.y,             r_mix.z,             0.f},
			{g_mix.x,             g_mix.y + rev_value, g_mix.z,             0.f},
			{b_mix.x,             b_mix.y,             b_mix.z + rev_value, 0.f},
			{0.f,                 0.f,                 0.f,                 1.f}
		);
		// clang-format on
	} else if (name == "hue-rotate") {
		filter.m_type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float s = Rml::Math::Sin(value);
		const float c = Rml::Math::Cos(value);
		// clang-format off
		filter.m_color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * c - 0.213f * s,  0.715f - 0.715f * c - 0.715f * s,  0.072f - 0.072f * c + 0.928f * s,  0.f},
			{0.213f - 0.213f * c + 0.143f * s,  0.715f + 0.285f * c + 0.140f * s,  0.072f - 0.072f * c - 0.283f * s,  0.f},
			{0.213f - 0.213f * c - 0.787f * s,  0.715f - 0.715f * c + 0.715f * s,  0.072f + 0.928f * c + 0.072f * s,  0.f},
			{0.f,                               0.f,                               0.f,                               1.f}
		);
		// clang-format on
	} else if (name == "saturate") {
		filter.m_type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		// clang-format off
		filter.m_color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * value,  0.715f - 0.715f * value,  0.072f - 0.072f * value,  0.f},
			{0.213f - 0.213f * value,  0.715f + 0.285f * value,  0.072f - 0.072f * value,  0.f},
			{0.213f - 0.213f * value,  0.715f - 0.715f * value,  0.072f + 0.928f * value,  0.f},
			{0.f,                      0.f,                      0.f,                      1.f}
		);
		// clang-format on
	}

	if (filter.m_type != FilterType::Invalid) {
		return reinterpret_cast<Rml::CompiledFilterHandle>(new CompiledFilter(std::move(filter)));
	}

	Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported filter type '%s'.", name.c_str());
	return {};
}

void RenderInterface_VK::ReleaseFilter(Rml::CompiledFilterHandle filter) {
	delete reinterpret_cast<CompiledFilter*>(filter);
}

void RenderInterface_VK::RenderBlur(float sigma, LayerPool& pool, int source_destination, int temp) {
	int pass_level = 0;
	SigmaToParameters(sigma, pass_level, sigma);

	const VkRect2D original_scissor = CurrentScissor();
	VkRect2D scissor = original_scissor;
	const VkExtent2D extent = pool.m_extent;

	// Downscale by iterative scaling with bilinear filtering to reduce aliasing
	const float uv_scale_x = (extent.width % 2 == 1) ? (1.f - 1.f / float(extent.width)) : 1.f;
	const float uv_scale_y = (extent.height % 2 == 1) ? (1.f - 1.f / float(extent.height)) : 1.f;

	const VkViewport half_viewport {0.f, 0.f, float(extent.width) / 2.f, float(extent.height) / 2.f, 0.f, 1.f};

	for (int i = 0; i < pass_level; i++) {
		scissor.offset.x = (scissor.offset.x + 1) / 2;
		scissor.offset.y = (scissor.offset.y + 1) / 2;
		scissor.extent.width = std::max(scissor.extent.width / 2, 1u);
		scissor.extent.height = std::max(scissor.extent.height / 2, 1u);

		const bool from_source = (i % 2 == 0);
		effect_image_t& src = pool.m_postprocess[from_source ? source_destination : temp];
		effect_image_t& dst = pool.m_postprocess[from_source ? temp : source_destination];

		effects_push_t push = {};
		push.m_v[0][0] = 0.f;
		push.m_v[0][1] = 0.f;
		push.m_v[0][2] = uv_scale_x;
		push.m_v[0][3] = uv_scale_y;
		push.m_v[1][0] = 1.f;
		RenderFullscreenPass(m_p_pipeline_passthrough_noblend, dst, src, nullptr, push, false, scissor, half_viewport);
	}

	// Move the downscaled result into the temp buffer
	if (pass_level % 2 == 0) {
		effects_push_t push = {};
		SetIdentityUv(push);
		push.m_v[1][0] = 1.f;
		RenderFullscreenPass(
		    m_p_pipeline_passthrough_noblend,
		    pool.m_postprocess[temp],
		    pool.m_postprocess[source_destination],
		    nullptr,
		    push,
		    false,
		    scissor,
		    FullViewport(extent)
		);
	}

	float weights[kBlurNumWeights];
	ComputeBlurWeights(sigma, weights);

	auto make_blur_push = [&](float texel_offset_x, float texel_offset_y) {
		effects_push_t push = {};
		SetIdentityUv(push);
		push.m_v[1][0] = texel_offset_x;
		push.m_v[1][1] = texel_offset_y;
		SetTexCoordLimits(push, scissor, extent);
		for (int i = 0; i < kBlurNumWeights; i++) {
			push.m_v[3][i] = weights[i];
		}
		return push;
	};

	// Vertical pass
	RenderFullscreenPass(
	    m_p_pipeline_blur,
	    pool.m_postprocess[source_destination],
	    pool.m_postprocess[temp],
	    nullptr,
	    make_blur_push(0.f, 1.f / float(extent.height)),
	    false,
	    scissor,
	    FullViewport(extent)
	);

	// Horizontal pass
	{
		VkRect2D padded = scissor;
		padded.offset.x = std::max(padded.offset.x - 1, 0);
		padded.offset.y = std::max(padded.offset.y - 1, 0);
		padded.extent.width += 2;
		padded.extent.height += 2;

		effects_push_t push = make_blur_push(1.f / float(extent.width), 0.f);

		effect_image_t& src = pool.m_postprocess[source_destination];
		effect_image_t& dst = pool.m_postprocess[temp];

		TransitionEffectImage(src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		VkDescriptorSet p_sets[] = {GetEffectDescriptorSet(src), GetEffectDescriptorSet(src)};

		BeginEffectScope(dst, false, FullRect(extent));

		VkClearAttachment clear = {};
		clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		clear.colorAttachment = 0;
		clear.clearValue.color = {
		  {0.f, 0.f, 0.f, 0.f}
		};
		VkClearRect clear_rect = {};
		clear_rect.layerCount = 1;
		clear_rect.rect = padded;
		vkCmdClearAttachments(m_p_current_command_buffer, 1, &clear, 1, &clear_rect);

		const VkViewport viewport = FullViewport(extent);
		vkCmdSetViewport(m_p_current_command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(m_p_current_command_buffer, 0, 1, &scissor);
		vkCmdBindPipeline(m_p_current_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_p_pipeline_blur);
		vkCmdBindDescriptorSets(
		    m_p_current_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_p_pipeline_layout_effects, 0, 2, p_sets, 0, nullptr
		);
		vkCmdPushConstants(
		    m_p_current_command_buffer,
		    m_p_pipeline_layout_effects,
		    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		    0,
		    sizeof(effects_push_t),
		    &push
		);
		vkCmdDraw(m_p_current_command_buffer, 3, 1, 0, 0);

		EndScope();
	}

	// Upscale the blurred region
	{
		effects_push_t push = {};
		push.m_v[0][0] = float(scissor.offset.x) / float(extent.width);
		push.m_v[0][1] = float(scissor.offset.y) / float(extent.height);
		push.m_v[0][2] = float(scissor.extent.width) / float(extent.width);
		push.m_v[0][3] = float(scissor.extent.height) / float(extent.height);
		push.m_v[1][0] = 1.f;

		const VkViewport region_viewport {
		  float(original_scissor.offset.x),
		  float(original_scissor.offset.y),
		  float(original_scissor.extent.width),
		  float(original_scissor.extent.height),
		  0.f,
		  1.f
		};

		RenderFullscreenPass(
		    m_p_pipeline_passthrough_noblend,
		    pool.m_postprocess[source_destination],
		    pool.m_postprocess[temp],
		    nullptr,
		    push,
		    false,
		    original_scissor,
		    region_viewport
		);
	}
}

void RenderInterface_VK::RenderFilters(Rml::Span<const Rml::CompiledFilterHandle> filter_handles) {
	LayerPool& pool = *m_p_current_pool;
	const VkRect2D scissor = CurrentScissor();
	const VkViewport viewport = FullViewport(pool.m_extent);

	for (int i = 0; i < 2; i++) {
		if (!pool.m_postprocess[i].m_p_image) {
			pool.m_postprocess[i] = CreateEffectImage(
			    pool.m_extent, m_color_attachment_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			);
		}
	}

	for (const Rml::CompiledFilterHandle filter_handle : filter_handles) {
		const CompiledFilter& filter = *reinterpret_cast<const CompiledFilter*>(filter_handle);

		switch (filter.m_type) {
			case FilterType::Passthrough: {
				effects_push_t push = {};
				SetIdentityUv(push);
				push.m_v[1][0] = filter.m_blend_factor;
				RenderFullscreenPass(
				    m_p_pipeline_passthrough_noblend, pool.m_postprocess[1], pool.m_postprocess[0], nullptr, push, true, scissor, viewport
				);
				std::swap(pool.m_postprocess[0], pool.m_postprocess[1]);
			} break;
			case FilterType::Blur: {
				RenderBlur(filter.m_sigma, pool, 0, 1);
			} break;
			case FilterType::DropShadow: {
				if (!pool.m_postprocess[2].m_p_image) {
					pool.m_postprocess[2] = CreateEffectImage(
					    pool.m_extent, m_color_attachment_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
					);
				}

				// Shadow silhouette into the secondary buffer
				{
					effects_push_t push = {};
					push.m_v[0][0] = -filter.m_offset.x / float(pool.m_extent.width);
					push.m_v[0][1] = -filter.m_offset.y / float(pool.m_extent.height);
					push.m_v[0][2] = 1.f;
					push.m_v[0][3] = 1.f;
					push.m_v[1][0] = float(filter.m_color.red) / 255.f;
					push.m_v[1][1] = float(filter.m_color.green) / 255.f;
					push.m_v[1][2] = float(filter.m_color.blue) / 255.f;
					push.m_v[1][3] = float(filter.m_color.alpha) / 255.f;
					SetTexCoordLimits(push, scissor, pool.m_extent);
					RenderFullscreenPass(
					    m_p_pipeline_dropshadow, pool.m_postprocess[1], pool.m_postprocess[0], nullptr, push, true, scissor, viewport
					);
				}

				if (filter.m_sigma >= 0.5f) {
					RenderBlur(filter.m_sigma, pool, 1, 2);
				}

				// Original content over the shadow
				{
					effects_push_t push = {};
					SetIdentityUv(push);
					push.m_v[1][0] = 1.f;
					RenderFullscreenPass(
					    m_p_pipeline_passthrough_blend,
					    pool.m_postprocess[1],
					    pool.m_postprocess[0],
					    nullptr,
					    push,
					    false,
					    scissor,
					    viewport
					);
				}

				std::swap(pool.m_postprocess[0], pool.m_postprocess[1]);
			} break;
			case FilterType::ColorMatrix: {
				effects_push_t push = {};
				SetIdentityUv(push);
				for (int row = 0; row < 4; row++) {
					for (int col = 0; col < 4; col++) {
						push.m_v[2 + row][col] = filter.m_color_matrix[col][row];
					}
				}
				RenderFullscreenPass(
				    m_p_pipeline_colormatrix, pool.m_postprocess[1], pool.m_postprocess[0], nullptr, push, true, scissor, viewport
				);
				std::swap(pool.m_postprocess[0], pool.m_postprocess[1]);
			} break;
			case FilterType::MaskImage: {
				effects_push_t push = {};
				SetIdentityUv(push);
				RenderFullscreenPass(
				    m_p_pipeline_blendmask,
				    pool.m_postprocess[1],
				    pool.m_postprocess[0],
				    &pool.m_blend_mask,
				    push,
				    true,
				    scissor,
				    viewport
				);
				std::swap(pool.m_postprocess[0], pool.m_postprocess[1]);
			} break;
			case FilterType::Invalid: {
				Rml::Log::Message(Rml::Log::LT_WARNING, "Unhandled render filter.");
			} break;
		}
	}
}

Rml::CompiledShaderHandle RenderInterface_VK::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) {
	gradient_data_std140_t data = {};
	bool valid = false;

	auto apply_color_stop_list = [&data](const Rml::Dictionary& shader_parameters) {
		auto it = shader_parameters.find("color_stop_list");
		RMLUI_ASSERT(it != shader_parameters.end() && it->second.GetType() == Rml::Variant::COLORSTOPLIST);
		const Rml::ColorStopList& color_stop_list = it->second.GetReference<Rml::ColorStopList>();
		const int num_stops = Rml::Math::Min(int(color_stop_list.size()), kMaxNumStops);

		data.m_num_stops = num_stops;
		for (int i = 0; i < num_stops; i++) {
			const Rml::ColorStop& stop = color_stop_list[i];
			RMLUI_ASSERT(stop.position.unit == Rml::Unit::NUMBER);
			data.m_stop_positions[i] = stop.position.number;
			for (int j = 0; j < 4; j++) {
				data.m_stop_colors[i][j] = (1.f / 255.f) * float(stop.color[j]);
			}
		}
	};

	if (name == "linear-gradient") {
		const bool repeating = Rml::Get(parameters, "repeating", false);
		data.m_func = repeating ? 3 : 0;
		const Rml::Vector2f p = Rml::Get(parameters, "p0", Rml::Vector2f(0.f));
		const Rml::Vector2f v = Rml::Get(parameters, "p1", Rml::Vector2f(0.f)) - p;
		data.m_p[0] = p.x;
		data.m_p[1] = p.y;
		data.m_v[0] = v.x;
		data.m_v[1] = v.y;
		apply_color_stop_list(parameters);
		valid = true;
	} else if (name == "radial-gradient") {
		const bool repeating = Rml::Get(parameters, "repeating", false);
		data.m_func = repeating ? 4 : 1;
		const Rml::Vector2f p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
		const Rml::Vector2f v = Rml::Vector2f(1.f) / Rml::Get(parameters, "radius", Rml::Vector2f(1.f));
		data.m_p[0] = p.x;
		data.m_p[1] = p.y;
		data.m_v[0] = v.x;
		data.m_v[1] = v.y;
		apply_color_stop_list(parameters);
		valid = true;
	} else if (name == "conic-gradient") {
		const bool repeating = Rml::Get(parameters, "repeating", false);
		data.m_func = repeating ? 5 : 2;
		const Rml::Vector2f p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
		const float angle = Rml::Get(parameters, "angle", 0.f);
		data.m_p[0] = p.x;
		data.m_p[1] = p.y;
		data.m_v[0] = Rml::Math::Cos(angle);
		data.m_v[1] = Rml::Math::Sin(angle);
		apply_color_stop_list(parameters);
		valid = true;
	}

	if (!valid) {
		Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported shader type '%s'.", name.c_str());
		return {};
	}

	// The parameter block lives in the memory pool for the whole shader lifetime
	auto* p_shader = new CompiledShader {};
	void* p_data = nullptr;
	bool status =
	    m_memory_pool.Alloc_GeneralBuffer(sizeof(gradient_data_std140_t), &p_data, &p_shader->m_buffer, &p_shader->m_allocation);
	RMLUI_VK_ASSERTMSG(status, "failed to allocate gradient parameter block");
	memcpy(p_data, &data, sizeof(data));

	return reinterpret_cast<Rml::CompiledShaderHandle>(p_shader);
}

void RenderInterface_VK::RenderShader(
    Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
    Rml::TextureHandle /*texture*/
) {
	if (m_p_current_command_buffer == nullptr || !shader || !geometry) {
		return;
	}

	const CompiledShader& compiled_shader = *reinterpret_cast<CompiledShader*>(shader);
	geometry_handle_t* p_geometry = reinterpret_cast<geometry_handle_t*>(geometry);

	// Same scheme as RenderGeometry
	m_user_data_for_vertex_shader.m_translate = translation;

	shader_vertex_user_data_t* p_data = nullptr;
	if (p_geometry->m_p_shader_allocation != nullptr) {
		m_memory_pool.Free_GeometryHandle_ShaderDataOnly(p_geometry);
	}

	bool status = m_memory_pool.Alloc_GeneralBuffer(
	    sizeof(m_user_data_for_vertex_shader),
	    reinterpret_cast<void**>(&p_data),
	    &p_geometry->m_p_shader,
	    &p_geometry->m_p_shader_allocation
	);
	RMLUI_VK_ASSERTMSG(status, "failed to allocate shader uniform data");

	p_data->m_transform = m_user_data_for_vertex_shader.m_transform;
	p_data->m_translate = m_user_data_for_vertex_shader.m_translate;

	const uint32_t p_offsets[] = {uint32_t(p_geometry->m_p_shader.offset), uint32_t(compiled_shader.m_buffer.offset)};
	VkDescriptorSet p_sets[] = {m_p_descriptor_set, m_p_descriptor_set_gradient};

	VkCommandBuffer cmd = m_p_current_command_buffer;

	const VkPipeline pipeline = m_is_apply_to_regular_geometry_stencil ? m_p_pipeline_gradient_stencil : m_p_pipeline_gradient;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_p_pipeline_layout_gradient, 0, 2, p_sets, 2, p_offsets);

	vkCmdBindVertexBuffers(cmd, 0, 1, &p_geometry->m_p_vertex.buffer, &p_geometry->m_p_vertex.offset);
	vkCmdBindIndexBuffer(cmd, p_geometry->m_p_index.buffer, p_geometry->m_p_index.offset, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, p_geometry->m_num_indices, 1, 0, 0, 0);
}

void RenderInterface_VK::ReleaseShader(Rml::CompiledShaderHandle shader) {
	if (auto* p_shader = reinterpret_cast<CompiledShader*>(shader)) {
		m_pending_for_deletion_shaders.push_back(p_shader);
	}
}
