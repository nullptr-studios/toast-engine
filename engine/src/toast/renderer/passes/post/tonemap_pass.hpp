/**
 * @file tonemap_pass.hpp
 * @author dario
 * @date 03/08/2026
 */

#pragma once

#include "toast/renderer/core/descriptor_cache.hpp"
#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/passes/post_process_pass_base.hpp"
#include "toast/renderer/resources/post_process_target.hpp"
#include "toast/renderer/settings/post_process_settings.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <tuple>
#include <vector>

namespace renderer {
class VulkanCore;

enum class TonemapMode : uint8_t {
	reinhard = 0,
	aces = 1,
	gt7 = 2,
};

class TonemapPass : public IPostProcessPass {
public:
	TonemapPass(const VulkanCore& core, vk::Format ldr_format, vk::Extent2D extent);

	[[nodiscard]]
	auto atOutputResolution() const -> bool override {
		return true;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Tonemap";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors tonemap.slang TonemapParams, 128 bytes under std140
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
		/// 1 writes scRGB nits
		uint32_t hdr = 0;
		float paper_white_scale = 1.0f;
		float peak_scale = 1.0f;
		float gt_toe = 1.28f;
		float chromatic_aberration = 0.0f;
		float temperature = 0.0f;
		float tint = 0.0f;
		/// w is the hue shift in degrees
		glm::vec4 color_filter {1.0f, 1.0f, 1.0f, 0.0f};
		glm::vec4 lift {0.0f};
		glm::vec4 gamma_inv {1.0f, 1.0f, 1.0f, 0.0f};
		/// w is the LUT intensity, 0 when no LUT is bound
		glm::vec4 gain {1.0f, 1.0f, 1.0f, 0.0f};
	};

	static_assert(sizeof(Params) == 128);

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

	/// Scene view, LUT view and the LUT's generation since a reloaded LUT can reuse a destroyed view handle
	DescriptorCache<std::tuple<vk::ImageView, vk::ImageView, uint64_t>> m_bound_views;

	ShaderLayout m_exposure_layout;
	VulkanPipeline m_histogram_pipeline;
	VulkanPipeline m_adapt_pipeline;

	std::optional<vma::raii::Buffer> m_histogram;

	/// One buffer unlike m_params_buffers since adaptation carries across frames
	std::optional<vma::raii::Buffer> m_exposure;

	std::vector<vk::raii::DescriptorSet> m_exposure_sets;
	DescriptorCache<vk::ImageView> m_exposure_bound_views;

	bool m_exposure_ready = false;
	bool m_was_metering = false;

	/// Generation of the last LUT warned about, so a wrong sized LUT warns once rather than every frame
	uint64_t m_rejected_lut = 0;
	std::chrono::steady_clock::time_point m_last_meter;
};

}
