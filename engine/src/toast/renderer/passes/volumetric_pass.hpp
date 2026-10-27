/**
 * @file volumetric_pass.hpp
 * @author dario
 * @date 26/10/2026
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

/// Height fog and fog volumes marched at half or quarter resolution
class VolumetricPass : public IPostProcessPass {
public:
	struct Quality {
		uint32_t steps = 24;
		uint32_t lights = 8;
		bool light_shadows = true;
		/// Full resolution pixels per scatter texel
		uint32_t divisor = 2;
	};

	VolumetricPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Volumetrics";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors volumetrics.slang VolumetricParams
	struct Params {
		glm::mat4 inverse_view_projection {1.0f};
		/// x extinction per meter y base height z height falloff w anisotropy
		glm::vec4 medium {0.0f};
		/// rgb scattering albedo a ambient scale
		glm::vec4 albedo {0.0f};
		/// x march distance y steps z 1 with a valid history w fog volumes
		glm::vec4 marching {0.0f};
		/// x full resolution pixels per texel y local lights per step z 1 when local lights cast shadows w fog volume distance
		glm::vec4 quality {0.0f};
	};

	[[nodiscard]]
	static auto resolveQuality() -> Quality;

	/// rgb in-scattered radiance a transmittance
	static constexpr vk::Format k_scatter_format = vk::Format::eR16G16B16A16Sfloat;

	static constexpr uint32_t k_image_bindings = 6;

	void createTargets(const VulkanCore& core, vk::Extent2D extent, uint32_t divisor);

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;
	bool m_ready = false;

	ShaderLayout m_layout;
	VulkanPipeline m_ambient_pipeline;
	VulkanPipeline m_scatter_pipeline;
	VulkanPipeline m_temporal_pipeline;
	VulkanPipeline m_composite_pipeline;
	SceneDescriptorSets m_scene_sets;

	vk::raii::Sampler m_point_sampler = nullptr;
	vk::raii::Sampler m_linear_sampler = nullptr;

	/// Per frame in flight
	std::vector<vk::raii::DescriptorSet> m_storage_sets;
	/// volumetrics.slang gFogAmbient per frame in flight
	std::vector<vma::raii::Buffer> m_ambient;
	std::vector<std::shared_ptr<const VoxelGpuStorage>> m_storage_bound;

	/// Per frame in flight then per history parity
	std::vector<vk::raii::DescriptorSet> m_image_sets;
	std::vector<std::array<vk::ImageView, k_image_bindings>> m_image_bound;

	PostProcessTarget m_scatter;
	/// The temporal pass reads one and writes the other
	std::array<PostProcessTarget, 2> m_history;
	PostProcessTarget m_target;

	vk::Extent2D m_extent {};
	uint32_t m_divisor = 2;
	uint32_t m_parity = 0;
	bool m_history_valid = false;
	uint64_t m_last_sequence = 0;
};

}
