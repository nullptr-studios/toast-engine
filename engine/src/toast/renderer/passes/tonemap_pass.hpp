/**
 * @file tonemap_pass.hpp
 * @author dario
 * @date 03/08/2026
 */

#pragma once

#include "../post_process_pass_base.hpp"
#include "../post_process_settings.hpp"
#include "../post_process_target.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace renderer {
class VulkanCore;

enum class TonemapMode : uint8_t {
	reinhard = 0,
	aces = 1,
};

class TonemapPass : public IPostProcessPass {
public:
	TonemapPass(const VulkanCore& core, vk::Format ldr_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Tonemap";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors tonemap.slang TonemapParams padded by std140 to 48 bytes
	struct Params {
		float exposure = 1.0f;
		uint32_t mode = static_cast<uint32_t>(TonemapMode::reinhard);
		float gamma = 2.2f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float vignette = 0.0f;
		float grain = 0.0f;
		float time = 0.0f;
		uint32_t auto_exposure = 0;
		std::array<float, 3> pad0 {};
	};

	/// Mirrors exposure.slang ExposureParams
	struct ExposureParams {
		float min_log = 0.0f;
		float log_range = 1.0f;
		float delta_time = 0.0f;
		float snap = 0.0f;
		float speed_up = 0.0f;
		float speed_down = 0.0f;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	void createResources(const VulkanCore& core);
	void createExposureResources(const VulkanCore& core);
	void createTarget(const VulkanCore& core, vk::Extent2D extent);

	/// Records the histogram and adaptation dispatches outside any rendering scope
	void recordExposure(
	    vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view, const PostProcessSettings::Tonemap& settings
	);

	const VulkanCore* m_core = nullptr;
	vk::Format m_ldr_format = vk::Format::eUndefined;

	PostProcessTarget m_target;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_pipeline;

	vk::raii::Sampler m_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	std::vector<FrameResources> m_params_buffers;

	std::vector<vk::ImageView> m_bound_views;

	ShaderLayout m_exposure_layout;
	VulkanPipeline m_histogram_pipeline;
	VulkanPipeline m_adapt_pipeline;

	std::optional<vma::raii::Buffer> m_histogram;

	/// One buffer unlike m_params_buffers since adaptation carries across frames
	std::optional<vma::raii::Buffer> m_exposure;

	std::vector<vk::raii::DescriptorSet> m_exposure_sets;
	std::vector<vk::ImageView> m_exposure_bound_views;

	bool m_exposure_ready = false;
	bool m_was_metering = false;
	std::chrono::steady_clock::time_point m_last_meter;
};

}
