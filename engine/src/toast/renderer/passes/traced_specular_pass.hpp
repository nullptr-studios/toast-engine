/**
 * @file traced_specular_pass.hpp
 * @author dario
 * @date 25/10/2026
 */

#pragma once

#include "../post_process_pass_base.hpp"
#include "../post_process_target.hpp"
#include "../scene_descriptor_set.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace renderer {
class VulkanCore;
class VoxelGpuStorage;

/// GGX rays for reflective voxels at half resolution in place of their environment specular
class TracedSpecularPass : public IPostProcessPass {
public:
	TracedSpecularPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Traced specular";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

	[[nodiscard]]
	auto isReady() const noexcept -> bool {
		return m_ready;
	}

private:
	/// Mirrors voxel_specular.slang SpecularParams
	struct Params {
		glm::mat4 inverse_view_projection {1.0f};
		/// x reach in meters y max roughness z 1 with a valid history w sun reach in meters
		glm::vec4 tracing {0.0f};
		/// x 1 when binding 10 holds last frame resolve
		glm::vec4 resolve {0.0f};
	};

	/// rgb radiance a hit distance then history and -1 where nothing was traced
	static constexpr vk::Format k_trace_format = vk::Format::eR16G16B16A16Sfloat;
	/// voxel_specular.slang packSurface
	static constexpr vk::Format k_surface_format = vk::Format::eR32G32B32A32Uint;

	static constexpr uint32_t k_image_bindings = 13;

	void createSets(const VulkanCore& core);
	void createTargets(const VulkanCore& core, vk::Extent2D extent);

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;
	bool m_ready = false;

	ShaderLayout m_layout;
	VulkanPipeline m_surface_pipeline;
	VulkanPipeline m_trace_pipeline;
	VulkanPipeline m_resolve_pipeline;
	VulkanPipeline m_temporal_pipeline;
	VulkanPipeline m_composite_pipeline;
	SceneDescriptorSets m_scene_sets;
	bool m_instances_bound = false;

	vk::raii::Sampler m_point_sampler = nullptr;

	/// Per frame in flight
	std::vector<vk::raii::DescriptorSet> m_storage_sets;
	std::vector<std::shared_ptr<const VoxelGpuStorage>> m_storage_bound;
	/// Per frame in flight then per history parity
	std::vector<vk::raii::DescriptorSet> m_image_sets;
	std::vector<std::array<vk::ImageView, k_image_bindings>> m_image_bound;

	PostProcessTarget m_surface;
	PostProcessTarget m_trace;
	PostProcessTarget m_resolved;
	/// The temporal pass reads one and writes the other
	std::array<PostProcessTarget, 2> m_history;
	PostProcessTarget m_target;

	uint32_t m_parity = 0;
	bool m_history_valid = false;
	/// A gap in frames or a recreated target restarts the history
	uint64_t m_last_sequence = 0;
};

}
