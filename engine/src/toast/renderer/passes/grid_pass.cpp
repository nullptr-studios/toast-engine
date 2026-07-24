#include "grid_pass.hpp"

#include "../shader_cache.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <array>
#include <cstring>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>

namespace renderer {

GridPass::GridPass(const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent) {
	const auto uid = assets::resolveURI("core://shaders/grid.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "GridPass shader core://shaders/grid.slang unavailable, the grid will not draw");
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "GridPass");

	const vk::VertexInputBindingDescription position_only_binding(0, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
	const std::vector<vk::VertexInputAttributeDescription> position_only_attributes {
	  vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, 0)
	};

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "GridPass";
	config.color_format = color_format;
	config.depth_format = depth_format;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.vertex_binding = position_only_binding;
	config.vertex_attributes = position_only_attributes;
	config.topology = vk::PrimitiveTopology::eTriangleList;
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = true;
	config.depth_write = false;
	config.blend_enable = true;
	m_pipeline.rebuild(core, config);

	createResources(core);
}

void GridPass::createResources(const renderer::VulkanCore& core) {
	const auto& device = core.getDevice();
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "GridPass shader layout has no descriptor sets");
		return;
	}

	const vk::DescriptorSetLayout frame_set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_frame_descriptor_sets.clear();
	m_frame_descriptor_sets.reserve(VulkanRenderer::k_frames_in_flight);

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &frame_set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_frame_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_frame_descriptor_sets[i], std::format("GridPass FrameSet[{}]", i));

		const auto* frame_res = VulkanRenderer::instance->getFrameUBORes(i);
		if (!frame_res->gpu_buffer.has_value()) {
			TOAST_CRITICAL("Render", "Frame UBO buffer missing for frame {}", i);
			continue;
		}

		const vk::DescriptorBufferInfo buffer_info(**frame_res->gpu_buffer, 0, sizeof(VulkanRenderer::FrameUBO));
		const vk::WriteDescriptorSet write(
		    *m_frame_descriptor_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_info
		);
		device.updateDescriptorSets(write, {});
	}

	// Fullscreen-ish quad in the XY plane, scaled/positioned per frame via push constants
	const std::array<glm::vec3, 6> vertices {
	  glm::vec3 {-1.0f, -1.0f, 0.0f},
	  glm::vec3 { 1.0f, -1.0f, 0.0f},
	  glm::vec3 { 1.0f,  1.0f, 0.0f},
	  glm::vec3 {-1.0f, -1.0f, 0.0f},
	  glm::vec3 { 1.0f,  1.0f, 0.0f},
	  glm::vec3 {-1.0f,  1.0f, 0.0f},
	};

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = sizeof(vertices);
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_vertex_buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *m_vertex_buffer, "GridPass VertexBuffer");

	void* mapped = m_vertex_buffer.getAllocation().getInfo().pMappedData;
	std::memcpy(mapped, vertices.data(), sizeof(vertices));
	m_vertex_buffer.getAllocation().flush(0, sizeof(vertices));
}

void GridPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	(void)image_index;

	if (!m_pipeline.isReady() || frame_index >= m_frame_descriptor_sets.size()) {
		return;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *m_shader_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_frame_descriptor_sets[frame_index]},
	    {}
	);

	constexpr float k_grid_half_extent = 1000.0f;    // matches grid.slang's fade-to-zero distance
	const glm::vec3 cam_pos = frame->frame_data.camera_position;

	DrawPushConstants pc {};
	pc.model = glm::translate(glm::mat4(1.0f), glm::vec3(cam_pos.x, cam_pos.y, 0.0f)) *
	           glm::scale(glm::mat4(1.0f), glm::vec3(k_grid_half_extent));
	cmd.pushConstants(
	    *m_shader_layout.getPipelineLayout(),
	    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
	    0,
	    sizeof(DrawPushConstants),
	    &pc
	);

	cmd.bindVertexBuffers(0, std::array<vk::Buffer, 1> {*m_vertex_buffer}, std::array<vk::DeviceSize, 1> {0});
	cmd.draw(6, 1, 0, 0);
}

}
