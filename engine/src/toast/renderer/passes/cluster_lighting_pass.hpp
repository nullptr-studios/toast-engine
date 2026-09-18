/// @file cluster_lighting_pass.hpp
/// @author dario
/// @date 18/07/2026

#pragma once
#include "../clustered_lighting_constants.hpp"
#include "../compute_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace renderer {
class VulkanCore;

class ClusterLightingPass : public IComputePass {
public:
	explicit ClusterLightingPass(const renderer::VulkanCore& core);

	void update(uint32_t frame_index, float dt) override;

	void dispatch(vk::CommandBuffer cmd, uint32_t frame_index) override;

	[[nodiscard]]
	auto getClusterParamsBuffer(uint32_t frame_index) const -> vk::Buffer;

	[[nodiscard]]
	auto getLightsBuffer(uint32_t frame_index) const -> vk::Buffer;

	[[nodiscard]]
	auto getClusterLightGridBuffer(uint32_t frame_index) const -> vk::Buffer;

	[[nodiscard]]
	auto getLightIndexListBuffer(uint32_t frame_index) const -> vk::Buffer;

	[[nodiscard]]
	auto getClusterLightGridCounts(uint32_t frame_index) const -> std::span<const uint32_t>;

private:
	/// Mirrors cluster_lighting.slang ClusterParams
	struct ClusterParamsGpu {
		glm::mat4 inverse_projection;
		glm::uvec4 cluster_dims;           // xyz dims w max lights per cluster
		glm::vec4 screen_size_near_far;    // xy screen size z near w far
		uint32_t light_count = 0;
		glm::vec3 _pad0 {0.0f};
	};

	/// Mirrors cluster_lighting.slang ClusterAABB
	struct ClusterAabbGpu {
		glm::vec4 min_point;
		glm::vec4 max_point;
	};

	struct FrameBuffers {
		FrameResources cluster_params;
		FrameResources lights;
		FrameResources cluster_aabb;
		FrameResources cluster_light_grid;
		FrameResources light_index_list;

		FrameResources cluster_light_grid_readback;
	};

	void createResources(const renderer::VulkanCore& core);

	[[nodiscard]]
	static auto createBuffer(const renderer::VulkanCore& core, vk::DeviceSize size, vk::BufferUsageFlags usage, bool host_visible)
	    -> vma::raii::Buffer;

	VulkanPipeline m_build_clusters_pipeline;
	VulkanPipeline m_cull_lights_pipeline;

	ShaderLayout m_shader_layout;
	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;

	std::vector<FrameBuffers> m_frame_buffers;
};

}
