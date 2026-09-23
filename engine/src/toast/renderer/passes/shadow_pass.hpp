/**
 * @file shadow_pass.hpp
 * @author dario
 * @date 01/08/2026
 */

#pragma once

#include "../render_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../shadow_constants.hpp"
#include "../voxel_change_history.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace renderer {
class VulkanCore;
class VoxelGpuStorage;

[[nodiscard]]
auto createShadowSampler(const VulkanCore& core, vk::Format format) -> vk::raii::Sampler;

class ShadowPass : public IRenderPass {
public:
	explicit ShadowPass(const VulkanCore& core);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "ShadowPass";
	}

	void recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	/// Everything records in recordPre()
	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override { }

	[[nodiscard]]
	auto getCascadeMapView(uint32_t frame_index) const -> vk::ImageView;

	[[nodiscard]]
	auto getPunctualMapView(uint32_t frame_index) const -> vk::ImageView;

	[[nodiscard]]
	auto getShadowSampler() const -> vk::Sampler {
		return *m_sampler;
	}

	[[nodiscard]]
	auto getDrawCount() const noexcept -> uint32_t {
		return m_draw_count;
	}

	[[nodiscard]]
	auto getPassCount() const noexcept -> uint32_t {
		return m_pass_count;
	}

	[[nodiscard]]
	auto getCachedCount() const noexcept -> uint32_t {
		return m_cached_count;
	}

	/// Cached groups that a voxel edit elsewhere would have re-rendered
	[[nodiscard]]
	auto getVoxelSparedCount() const noexcept -> uint32_t {
		return m_voxel_spared_count;
	}

private:
	/// Mirrors voxel_shadow.slang ShadowUBO and shadow_depth.slang reads only the first array
	struct ShadowUBO {
		std::array<glm::mat4, shadows::k_max_shadow_views> view_projection {};
		std::array<glm::mat4, shadows::k_max_shadow_views> inverse_view_projection {};
	};

	/// Mirrors shadow_depth.slang PushConstants
	struct ShadowPushConstants {
		uint32_t instance_base = 0;
		uint32_t view_index = 0;
	};

	/// Mirrors voxel_shadow.slang PushConstants
	struct VoxelShadowPush {
		uint32_t instance_index = 0;
		uint32_t view_index = 0;
		float constant_bias = 0.0f;
		uint32_t pad0 = 0;
	};

	struct VoxelPipelineSet {
		uint32_t view_mask = 1;

		/// Indexed [cull front][exact depth] since mirroring flips the winding
		std::array<std::array<VulkanPipeline, 2>, 2> pipelines;
	};

	static constexpr uint32_t k_max_voxel_casters = 4096;

	struct LayerGroup {
		uint32_t base_layer = 0;
		uint32_t layer_count = 1;

		/// Never 0 since that disables multiview
		uint32_t view_mask = 1;

		std::optional<vk::raii::ImageView> view;

		bool dirty = true;

		std::optional<uint64_t> signature;

		/// Voxel storage this group last matched
		std::optional<uint64_t> voxel_generation;
	};

	struct ShadowMap {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> array_view;
		std::vector<LayerGroup> groups;
		vk::ImageLayout layout = vk::ImageLayout::eUndefined;
		uint32_t resolution = 0;
		uint32_t layer_count = 0;
	};

	struct PipelineSet {
		uint32_t view_mask = 1;
		VulkanPipeline pipeline;
	};

	struct FrameTarget {
		ShadowMap cascades;
		ShadowMap punctual;
		FrameResources ubo;
	};

	void createResources(const VulkanCore& core);

	void createShadowMap(
	    const VulkanCore& core, ShadowMap& map, uint32_t resolution, std::span<const uint32_t> group_layers,
	    std::string_view debug_name
	);

	[[nodiscard]]
	auto pipelineSetFor(uint32_t view_mask) const -> const PipelineSet*;

	void createVoxelResources(const VulkanCore& core);

	/// @returns the voxel instances written for this slot
	[[nodiscard]]
	auto prepareVoxels(uint32_t frame_index) -> uint32_t;

	[[nodiscard]]
	auto voxelPipelineSetFor(uint32_t view_mask) const -> const VoxelPipelineSet*;

	void recordMap(vk::CommandBuffer cmd, ShadowMap& map, uint32_t frame_index, bool directional);

	[[nodiscard]]
	static auto selectShadowFormat(const VulkanCore& core) -> vk::Format;

	const VulkanCore* m_core = nullptr;
	vk::Format m_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;

	/// VulkanPipeline is not movable
	std::array<PipelineSet, 3> m_pipeline_sets;

	uint32_t m_draw_count = 0;
	uint32_t m_pass_count = 0;
	uint32_t m_cached_count = 0;
	uint32_t m_voxel_spared_count = 0;
	std::vector<VoxelCaster> m_voxel_eligible;

	vk::raii::Sampler m_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	std::vector<FrameTarget> m_targets;

	ShaderLayout m_voxel_layout;
	std::array<VoxelPipelineSet, 3> m_voxel_pipeline_sets;
	std::vector<vk::raii::DescriptorSet> m_voxel_shadow_sets;
	std::vector<vk::raii::DescriptorSet> m_voxel_storage_sets;
	std::vector<vma::raii::Buffer> m_voxel_instance_buffers;
	std::vector<std::shared_ptr<const VoxelGpuStorage>> m_voxel_bound_storage;
	bool m_voxels_ready = false;

	/// Depth units of the rasterizer constant bias since SV_Depth skips it
	float m_voxel_constant_bias = 0.0f;

	uint32_t m_voxel_instance_count = 0;

	std::vector<uint32_t> m_voxel_draw_order;
};

}
