/// @file cluster_lighting_pass.hpp
/// @author dario
/// @date 18/07/2026

#pragma once
#include "../clustered_lighting_constants.hpp"
#include "../compute_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <atomic>
#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace renderer {
class VulkanCore;

class ClusterLightingPass : public IComputePass {
public:
	struct GridStats {
		uint32_t occupied = 0;
		float mean = 0.0f;
		uint32_t p95 = 0;
		uint32_t max_count = 0;
		uint32_t overflowed = 0;
		uint32_t dropped = 0;
	};

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

	/// The grid is only copied back for a few frames after each call
	void requestGridReadback() const noexcept;

	/// Unclamped counts and empty until a copy for this frame slot has landed
	[[nodiscard]]
	auto getClusterLightGridCounts(uint32_t frame_index) const -> std::span<const uint32_t>;

	[[nodiscard]]
	static auto summarizeGrid(std::span<const uint32_t> counts) -> GridStats;

private:
	/// Mirrors cluster_lighting.slang ClusterParams
	struct ClusterParamsGpu {
		glm::mat4 inverse_projection;
		glm::uvec4 cluster_dims;           // xyz dims w max lights per cluster
		glm::vec4 screen_size_near_far;    // xy screen size z camera near w camera far
		glm::vec4 slice_depth;             // x log range start y log range end z slices per log unit
		glm::uvec4 light_counts;           // x light count
	};

	static_assert(sizeof(ClusterParamsGpu) == 128, "ClusterParamsGpu must match the ClusterParams layout in the shaders");

	enum class HostAccess : uint8_t {
		none,
		write,
		read,
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
		bool readback_valid = false;
	};

	void createResources(const renderer::VulkanCore& core);

	[[nodiscard]]
	static auto createBuffer(const renderer::VulkanCore& core, vk::DeviceSize size, vk::BufferUsageFlags usage, HostAccess access)
	    -> vma::raii::Buffer;

	VulkanPipeline m_build_clusters_pipeline;
	VulkanPipeline m_cull_lights_pipeline;

	ShaderLayout m_shader_layout;
	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;

	std::vector<FrameBuffers> m_frame_buffers;

	mutable std::atomic<uint32_t> m_readback_hold {0};
};

}
