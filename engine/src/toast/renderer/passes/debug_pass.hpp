/// @file debug_pass.hpp
/// @author dario
/// @date 10/06/2026

#pragma once
#include "../render_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <toast/world/gizmo_layout.hpp>
#include <unordered_map>
#include <vector>

namespace renderer {
class VulkanCore;
class ClusterLightingPass;

class DebugPass : public IRenderPass {
public:
	DebugPass(
	    const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent,
	    const ClusterLightingPass* cluster_lighting_pass = nullptr
	);

	~DebugPass() override;

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return RenderStage::overlay;
	}

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	void update(uint32_t frame_index, float dt) override;

	void setEditorPanelsEnabled(bool enabled) noexcept { m_editor_panels = enabled; }

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Debug";
	}

private:
	struct DrawPushConstants {
		glm::mat4 model;
		glm::vec4 tint {1.0f};
	};

	struct GizmoHandleRange {
		uint32_t first_vertex = 0;
		uint32_t vertex_count = 0;
		glm::vec4 base_color {1.0f};
	};

	struct DynamicVertexBuffer {
		vma::raii::Buffer buffer = nullptr;
		void* mapped = nullptr;
		vk::DeviceSize capacity_bytes = 0;
	};

	void createResources(const renderer::VulkanCore& core);

	void initImGui(const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format);
	void createGizmoGeometry(const renderer::VulkanCore& core);
	void createTranslateGizmoGeometry(const renderer::VulkanCore& core);
	void createRotateGizmoGeometry(const renderer::VulkanCore& core);
	void createScaleGizmoGeometry(const renderer::VulkanCore& core);

	void createBillboardResources(
	    const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent
	);

	auto billboardTextureSet(const renderer::VulkanCore& core, vk::ImageView view) -> vk::DescriptorSet;

	void ensureLineCapacity(const renderer::VulkanCore& core, DynamicVertexBuffer& buffer, size_t required_vertex_count);

	VulkanPipeline m_line_pipeline;
	VulkanPipeline m_fill_pipeline;
	VulkanPipeline m_gizmo_pipeline;

	ShaderLayout m_shader_layout;
	std::vector<vk::raii::DescriptorSet> m_frame_descriptor_sets;

	std::vector<DynamicVertexBuffer> m_line_vertex_buffers;
	std::vector<uint32_t> m_line_vertex_counts;
	std::vector<DynamicVertexBuffer> m_fill_vertex_buffers;
	std::vector<uint32_t> m_fill_vertex_counts;

	std::vector<uint32_t> m_fill_sort_order;
	std::vector<float> m_fill_sort_depths;

	vma::raii::Buffer m_gizmo_vertex_buffer = nullptr;
	uint32_t m_gizmo_vertex_count = 0;

	vma::raii::Buffer m_translate_gizmo_vertex_buffer = nullptr;
	std::array<GizmoHandleRange, 7> m_translate_gizmo_handles;

	vma::raii::Buffer m_rotate_gizmo_vertex_buffer = nullptr;
	std::array<GizmoHandleRange, 7> m_rotate_gizmo_handles;

	vma::raii::Buffer m_scale_gizmo_vertex_buffer = nullptr;
	std::array<GizmoHandleRange, 7> m_scale_gizmo_handles;

	VulkanPipeline m_mesh_pipeline;

	struct BillboardPushConstants {
		glm::vec4 center_size;    // xyz centre w edge length
		glm::vec4 tint {1.0f};
	};

	VulkanPipeline m_billboard_pipeline;
	ShaderLayout m_billboard_layout;
	std::vector<vk::raii::DescriptorSet> m_billboard_frame_sets;
	vk::raii::Sampler m_billboard_sampler = nullptr;
	std::unordered_map<VkImageView, vk::raii::DescriptorSet> m_billboard_texture_sets;

	bool m_imgui_ready = false;
	bool m_editor_panels = true;

	struct PerfOverlay;
	std::unique_ptr<PerfOverlay> m_perf;

	void drawPerformanceWindow();

	const ClusterLightingPass* m_cluster_lighting_pass = nullptr;

	float m_sky_intensity_ui = -1.0f;

	std::array<char, 256> m_environment_uri_ui {};
};

}
