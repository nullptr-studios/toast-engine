/// @file debug_pass.hpp
/// @author dario
/// @date 10/06/2026.

#pragma once
#include "../render_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace renderer {
class VulkanCore;

/**
 * @brief Editor/debug visualization pass: ground grid, immediate-mode debug lines, and axis gizmos
 *
 * Debug lines and gizmo transforms aren't owned by this class - they're queued via the free functions in
 * vulkan_renderer.hpp (debugDrawLine(), debugDrawBox(), debugDrawSphere(), debugDrawAxes()) between
 * beginFrameBuild() and submitFrame(), exactly like MeshInstanceProxy. They flow through the same
 * VulkanRenderer::RenderFrame snapshot
 */
class DebugPass : public IRenderPass {
public:
	DebugPass(const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent);

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	void update(uint32_t frame_index, float dt) override;

	/// @brief Toggles the ground grid
	void setGridEnabled(bool enabled) { m_grid_enabled = enabled; }

	[[nodiscard]]
	auto gridEnabled() const -> bool {
		return m_grid_enabled;
	}

private:
	struct DrawPushConstants {
		glm::mat4 model;
	};

	/// @brief A vk::raii-owned buffer that can grow
	struct DynamicVertexBuffer {
		vma::raii::Buffer buffer = nullptr;
		void* mapped = nullptr;
		vk::DeviceSize capacity_bytes = 0;
	};

	void createResources(const renderer::VulkanCore& core);
	void createGridGeometry(const renderer::VulkanCore& core);
	void createGizmoGeometry(const renderer::VulkanCore& core);

	/// @brief Grows @p buffer so it can hold at least @p required_vertex_count DebugVertex entries
	void ensureLineCapacity(const renderer::VulkanCore& core, DynamicVertexBuffer& buffer, size_t required_vertex_count);

	VulkanPipeline m_plane_pipeline;
	VulkanPipeline m_line_pipeline;
	VulkanPipeline m_gizmo_pipeline;

	ShaderLayout m_shader_layout;
	std::vector<vk::raii::DescriptorSet> m_frame_descriptor_sets;

	// Ground grid
	bool m_grid_enabled = true;
	vma::raii::Buffer m_grid_vertex_buffer = nullptr;

	// Debug lines
	std::vector<DynamicVertexBuffer> m_line_vertex_buffers;
	std::vector<uint32_t> m_line_vertex_counts;

	// Gizmo axis triad
	vma::raii::Buffer m_gizmo_vertex_buffer = nullptr;
	uint32_t m_gizmo_vertex_count = 0;
};

}
