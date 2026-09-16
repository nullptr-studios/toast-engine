/**
 * @file depth_prepass.cpp
 * @author dario
 * @date 13/08/2026
 */

#include "depth_prepass.hpp"

#include "../shader_cache.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_mesh.hpp"
#include "../vulkan_renderer.hpp"

#include <array>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/assets/material.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

DepthPrepass::DepthPrepass(const VulkanCore& core, vk::Format depth_format, vk::Extent2D extent) : m_core(&core) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/depth_prepass.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "DepthPrepass shader unavailable; the depth prepass is disabled");
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "DepthPrepass");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "DepthPrepass";
	config.depth_format = depth_format;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.vertex_bindings = {vertexBindingDescription()};
	const auto vertex_attributes = vertexAttributeDescriptions();
	config.vertex_attributes.assign(vertex_attributes.begin(), vertex_attributes.end());
	config.cull_mode = vk::CullModeFlagBits::eBack;
	config.depth_test = true;
	config.depth_write = true;
	config.depth_only = true;
	m_pipeline.rebuild(core, config);

	createResources(core);
	TOAST_INFO("Render", "DepthPrepass ready");
}

void DepthPrepass::createResources(const VulkanCore& core) {
	ZoneScoped;
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		return;
	}

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const auto& device = core.getDevice();

	m_descriptor_sets.clear();
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("DepthPrepass DescriptorSet[{}]", i));

		const auto* frame_res = VulkanRenderer::instance->getFrameUBORes(i);
		if (frame_res == nullptr || !frame_res->gpu_buffer.has_value()) {
			continue;
		}

		const std::array buffer_infos {
		  vk::DescriptorBufferInfo(**frame_res->gpu_buffer, 0, sizeof(VulkanRenderer::FrameUBO)),
		  vk::DescriptorBufferInfo(
		      VulkanRenderer::instance->getInstanceBuffer(i),
		      0,
		      sizeof(VulkanRenderer::InstanceData) * VulkanRenderer::k_max_instances
		  )
		};

		const std::array writes {
		  vk::WriteDescriptorSet(*m_descriptor_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, buffer_infos.data()),
		  vk::WriteDescriptorSet(*m_descriptor_sets[i], 13, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[1])
		};
		device.updateDescriptorSets(writes, {});
	}
}

void DepthPrepass::record(vk::CommandBuffer cmd, uint32_t frame_index) {
	ZoneScoped;
	m_drawn = 0;
	if (!m_pipeline.isReady() || frame_index >= m_descriptor_sets.size()) {
		return;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->mesh_instances.empty()) {
		return;
	}

	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *m_shader_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_descriptor_sets[frame_index]},
	    {}
	);

	VulkanMesh* bound_mesh = nullptr;
	uint32_t bound_posed_offset = VulkanRenderer::MeshInstanceProxy::k_no_posed_vertices;
	const vk::Buffer posed_vertices = VulkanRenderer::instance->getPosedVertexBuffer(frame_index);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());

	for (size_t i = 0; i < frame->mesh_instances.size();) {
		const auto& proxy = frame->mesh_instances[i];

		const bool eligible = proxy.visible && proxy.mesh != nullptr && proxy.mesh->isReady() && proxy.root_material != nullptr &&
		                      proxy.root_material->settings().blend_mode == assets::BlendMode::opaque &&
		                      !VulkanRenderer::instance->materialUsesCutout(proxy.root_material);
		if (!eligible) {
			++i;
			continue;
		}

		const bool posed = proxy.posed_vertex_offset != VulkanRenderer::MeshInstanceProxy::k_no_posed_vertices && posed_vertices;

		uint32_t run = 1;
		if (!posed) {
			while (i + run < frame->mesh_instances.size()) {
				const auto& next = frame->mesh_instances[i + run];
				const bool next_posed = next.posed_vertex_offset != VulkanRenderer::MeshInstanceProxy::k_no_posed_vertices;
				if (next.mesh != proxy.mesh || !next.visible || next_posed || next.instance_index != proxy.instance_index + run) {
					break;
				}
				++run;
			}
		}

		cmd.pushConstants(
		    *m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(uint32_t), &proxy.instance_index
		);

		if (bound_mesh != proxy.mesh || bound_posed_offset != proxy.posed_vertex_offset) {
			if (posed) {
				proxy.mesh->bindPosed(cmd, posed_vertices, proxy.posed_vertex_offset);
			} else {
				proxy.mesh->bind(cmd);
			}
			bound_mesh = proxy.mesh;
			bound_posed_offset = proxy.posed_vertex_offset;
		}
		proxy.mesh->draw(cmd, run);

		m_drawn += run;
		i += run;
	}
}

}
