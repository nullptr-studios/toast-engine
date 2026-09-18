/**
 * @file environment_pass.hpp
 * @author dario
 * @date 03/08/2026
 */

#pragma once

#include "../cubemap_target.hpp"
#include "../render_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <toast/assets/hdr_image.hpp>
#include <vector>

namespace renderer {
class VulkanCore;

class EnvironmentPass : public IRenderPass {
public:
	EnvironmentPass(const VulkanCore& core, vk::Format hdr_format, vk::Format depth_format);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Environment";
	}

	void recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return RenderStage::world;
	}

	[[nodiscard]]
	auto getIrradianceView() const -> vk::ImageView;

	[[nodiscard]]
	auto getPrefilteredView() const -> vk::ImageView;

	[[nodiscard]]
	auto getSampler() const -> vk::Sampler {
		return *m_sampler;
	}

	[[nodiscard]]
	auto getPrefilteredMipCount() const noexcept -> uint32_t {
		return m_prefiltered_mips;
	}

	[[nodiscard]]
	auto isReady() const noexcept -> bool {
		return m_has_data;
	}

	[[nodiscard]]
	auto getSkyIntensity() const noexcept -> float {
		return m_intensity;
	}

	void setSkyIntensity(float intensity) noexcept {
		if (intensity == m_intensity) {
			return;
		}
		m_intensity = intensity;
		m_precompute_pending = true;
	}

	auto setEnvironmentMap(std::string_view uri) -> bool;

	[[nodiscard]]
	auto getEnvironmentMapUri() const -> const std::string& {
		return m_environment_uri;
	}

private:
	/// Mirrors environment.slang EnvironmentParams
	struct Params {
		glm::vec4 face_right {0.0f};
		glm::vec4 face_up {0.0f};
		glm::vec4 face_forward {0.0f};
		float roughness = 0.0f;
		float intensity = 1.0f;
		glm::vec2 _pad0 {0.0f};
	};

	void createPipelines(const VulkanCore& core);
	void createDescriptors(const VulkanCore& core);

	void renderFace(
	    vk::CommandBuffer cmd, const VulkanPipeline& pipeline, vk::DescriptorSet set, const CubemapTarget& target, uint32_t mip,
	    uint32_t face, const Params& params
	);

	const VulkanCore* m_core = nullptr;
	vk::Format m_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_sky_pipeline;
	VulkanPipeline m_equirect_pipeline;
	VulkanPipeline m_irradiance_pipeline;
	VulkanPipeline m_prefilter_pipeline;
	VulkanPipeline m_skybox_pipeline;
	vk::Format m_depth_format = vk::Format::eUndefined;

	vk::raii::Sampler m_sampler = nullptr;

	CubemapTarget m_sky;
	CubemapTarget m_irradiance;
	CubemapTarget m_prefiltered;

	vk::raii::DescriptorSet m_sky_source_set = nullptr;

	std::optional<vma::raii::Image> m_equirect_image;
	std::optional<vk::raii::ImageView> m_equirect_view;
	vk::raii::Sampler m_equirect_sampler = nullptr;

	/// Binding 0 is the black cube since a descriptor may not point at the image being written
	vk::raii::DescriptorSet m_equirect_set = nullptr;
	std::string m_environment_uri;
	bool m_has_equirect = false;

	void createEquirectPlaceholder(const VulkanCore& core);
	void writeEquirectSet(const VulkanCore& core);
	auto uploadEquirect(const assets::HdrImage& image) -> bool;

	uint32_t m_prefiltered_mips = 1;
	bool m_precompute_pending = true;
	bool m_has_data = false;

	float m_intensity = 3.0f;
};

}
