/**
 * @file reflection_probe_pass.hpp
 * @author dario
 * @date 05/08/2026
 */

#pragma once

#include "../cubemap_target.hpp"
#include "../render_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace renderer {
class VulkanCore;

class ReflectionProbePass : public IRenderPass {
public:
	/// Matches VulkanRenderer::k_max_reflection_probes
	static constexpr uint32_t k_max_probes = 8;

	static constexpr uint32_t k_default_face_size = 128;

	static constexpr uint32_t k_max_face_size = 512;

	static constexpr uint32_t k_irradiance_size = 32;

	ReflectionProbePass(const VulkanCore& core, vk::Format color_format, vk::Format depth_format);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Reflection Probes";
	}

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	void recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	void queueSave(uint32_t probe, std::string_view uri);

	auto loadProbe(uint32_t probe, std::string_view uri) -> bool;

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return RenderStage::world;
	}

	/// @param scene_color Must be eTransferSrcOptimal
	void captureFace(vk::CommandBuffer cmd, vk::Image scene_color, vk::Extent2D scene_extent, uint32_t probe, uint32_t face);

	void captureIrradianceFace(vk::CommandBuffer cmd, vk::Image scene_color, vk::Extent2D scene_extent, uint32_t face);

	void projectStagingToSh(vk::CommandBuffer cmd, uint32_t probe);

	[[nodiscard]]
	auto getShBuffer() const -> vk::Buffer;

	void queueShSave(uint32_t base, uint32_t count, std::string_view uri, const ShGridKey& key);

	auto loadShRange(uint32_t base, uint32_t count, std::string_view uri, const ShGridKey& key) -> bool;

	/// @warning Blocks on device idle
	void setProbeResolution(uint32_t probe, uint32_t face_size);

	[[nodiscard]]
	auto getProbeResolution(uint32_t probe) const -> uint32_t;

	[[nodiscard]]
	auto getProbeView(uint32_t probe) const -> vk::ImageView;

	[[nodiscard]]
	auto getProbeIrradianceView(uint32_t probe) const -> vk::ImageView;

	[[nodiscard]]
	auto getSampler() const -> vk::Sampler {
		return *m_sampler;
	}

	[[nodiscard]]
	auto getMipCount() const noexcept -> uint32_t {
		return m_mip_levels;
	}

	[[nodiscard]]
	auto isBaked(uint32_t probe) const -> bool;

	void setBakedTransform(uint32_t probe, const glm::vec3& position, const glm::vec3& extents);

	[[nodiscard]]
	auto isStale(uint32_t probe, const glm::vec3& position, const glm::vec3& extents) const -> bool;

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

	struct ProbeCube {
		CubemapTarget cube;
		bool baked = false;
		glm::vec3 baked_position {0.0f};
		glm::vec3 baked_extents {0.0f};
	};

	void createPipelines(const VulkanCore& core);
	void createDescriptors(const VulkanCore& core);

	void rebindProbeViews(uint32_t probe);

	[[nodiscard]]
	static auto cubeByteSize(const ProbeCube& cube) -> vk::DeviceSize;

	auto readbackCube(ProbeCube& cube, std::vector<uint8_t>& out) -> bool;

	auto uploadCube(ProbeCube& cube, const std::vector<uint8_t>& bytes, size_t offset) -> bool;

	std::vector<std::pair<uint32_t, std::string>> m_pending_saves;

	struct PendingShSave {
		uint32_t base = 0;
		uint32_t count = 0;
		std::string uri;
		ShGridKey key;
	};

	std::vector<PendingShSave> m_pending_sh_saves;

	void prefilterInto(vk::CommandBuffer cmd, uint32_t probe);

	void convolveIrradianceInto(vk::CommandBuffer cmd, uint32_t probe);

	void renderFace(
	    vk::CommandBuffer cmd, const VulkanPipeline& pipeline, const ProbeCube& target, uint32_t size, uint32_t mip, uint32_t face,
	    float roughness
	);

	const VulkanCore* m_core = nullptr;
	vk::Format m_format = vk::Format::eUndefined;
	uint32_t m_mip_levels = 1;

	vk::raii::Sampler m_sampler = nullptr;
	std::vector<ProbeCube> m_probes;

	std::vector<ProbeCube> m_probe_irradiance;

	ProbeCube m_staging;

	void createShResources(const VulkanCore& core);

	/// 4 float4 each L0 L1y L1z L1x
	std::optional<vma::raii::Buffer> m_sh_buffer;
	ShaderLayout m_sh_layout;
	VulkanPipeline m_sh_pipeline;
	vk::raii::DescriptorSet m_sh_set = nullptr;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_prefilter_pipeline;
	VulkanPipeline m_irradiance_pipeline;
	vk::raii::DescriptorSet m_staging_source_set = nullptr;

	/// Mirrors probe_preview.slang ProbePreviewParams
	struct PreviewParams {
		glm::mat4 view_projection {1.0f};
		glm::vec4 center_radius {0.0f};
		glm::vec4 camera_position {0.0f};
	};

	vk::Format m_depth_format = vk::Format::eUndefined;
	ShaderLayout m_preview_layout;
	VulkanPipeline m_preview_pipeline;

	std::vector<vk::raii::DescriptorSet> m_preview_sets;
};

}
