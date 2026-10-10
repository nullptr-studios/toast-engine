/**
 * @file voxel_pass.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once

#include "../render_pass_base.hpp"
#include "../scene_descriptor_set.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <string_view>
#include <vector>

namespace renderer {
class VulkanCore;
class VoxelGpuStorage;

/// Deferred so lighting runs once per visible voxel pixel not once per march
class VoxelPass : public IRenderPass {
public:
	VoxelPass(const VulkanCore& core, vk::Format scene_format, vk::Format depth_format, vk::Extent2D extent);

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return RenderStage::voxel_gbuffer;
	}

	/// @returns false when nothing draws so the caller skips both voxel scopes
	[[nodiscard]]
	auto prepare(uint32_t frame_index) -> bool;

	/// Inside the voxel G-buffer scope after prepare()
	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	/// Writes scene normal while reading face normals and depth
	void recordNormalBlur(vk::CommandBuffer cmd, uint32_t frame_index, vk::Extent2D viewport);

	/// Writes scene colour and indirect while reading the G-buffer and depth
	void recordLighting(vk::CommandBuffer cmd, uint32_t frame_index);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Voxels";
	}

	static constexpr uint32_t k_max_instances = 4096;

private:
	/// Throttles the over limit warning
	std::chrono::steady_clock::time_point m_last_limit_warning;

	/// Mirrors voxel.slang PushConstants
	struct PushConstants {
		uint32_t instance_index = 0;
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
		uint32_t pad2 = 0;
	};

	/// Mirrors voxel_lighting.slang LightingPushConstants
	struct LightingPushConstants {
		glm::mat4 inverse_view_projection {1.0f};
	};

	/// Mirrors voxel_normal_blur.slang NormalBlurParams
	struct NormalBlurParams {
		glm::mat4 inverse_view_projection {1.0f};
		glm::vec4 camera_radius {0.0f};
		glm::vec4 viewport {0.0f};
	};

	enum class Cull : uint8_t {
		back,
		front,
	};

	enum class Depth : uint8_t {
		/// SV_DepthGreaterEqual with the camera outside
		conservative,
		/// SV_Depth with the camera inside
		exact,
	};

	struct Draw {
		uint32_t instance = 0;
		Cull cull = Cull::back;
		Depth depth = Depth::conservative;
	};

	void createInstanceBuffers(const VulkanCore& core);
	void createDescriptors(const VulkanCore& core, const ShaderReflection& reflection);
	void createLighting(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);
	void createNormalBlur(const VulkanCore& core, vk::Extent2D extent);

	void bindStorage(uint32_t frame_index, const std::shared_ptr<const VoxelGpuStorage>& storage);

	const VulkanCore* m_core = nullptr;
	ShaderLayout m_shader_layout;

	/// Indexed [cull][depth]
	std::array<std::array<VulkanPipeline, 2>, 2> m_pipelines;

	SceneDescriptorSets m_scene_sets;

	std::vector<vk::raii::DescriptorSet> m_storage_sets;

	std::vector<vma::raii::Buffer> m_instance_buffers;

	std::vector<std::shared_ptr<const VoxelGpuStorage>> m_bound_storage;

	std::vector<Draw> m_draws;

	ShaderLayout m_lighting_layout;
	VulkanPipeline m_lighting_pipeline;
	SceneDescriptorSets m_lighting_scene_sets;

	vk::raii::Sampler m_point_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_gbuffer_sets;

	/// Albedo normal material depth and face normal last written into each set
	std::vector<std::array<vk::ImageView, 5>> m_bound_gbuffer;

	ShaderLayout m_blur_layout;
	VulkanPipeline m_blur_pipeline;
	std::vector<vk::raii::DescriptorSet> m_blur_sets;

	/// Albedo face normal and depth last written into each set
	std::vector<std::array<vk::ImageView, 3>> m_bound_blur;
};

}
