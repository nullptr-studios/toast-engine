/**
 * @file dlss_upscaler.hpp
 * @author dario
 * @date 22/11/2026
 */

#pragma once

#include "toast/renderer/core/vulkan_common.hpp"

#include <glm/glm.hpp>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

struct NVSDK_NGX_Parameter; 
struct NVSDK_NGX_Handle;

namespace renderer {
class VulkanCore;

/// Every image in a shader read layout visible to compute except output, which is in eGeneral
struct DlssInputs {
	vk::ImageView color, depth, motion, reactive, output;
	vk::Image color_image, depth_image, motion_image, reactive_image, output_image;
	/// Shared by color and output
	vk::Format color_format = vk::Format::eUndefined;
	vk::Format depth_format = vk::Format::eUndefined;
	vk::Extent2D render_extent, output_extent;
	/// Offset the projection moved the scene by this frame, in render pixels with y down
	glm::vec2 jitter_pixels {0.0f};
	bool reset = false;
};

/// NVIDIA DLSS Super Resolution through NGX, every call is a no-op in builds without TOAST_DLSS
class DlssUpscaler {
public:
	/// What NGX needs at vkCreateInstance, before any GPU is known. Empty when the query fails
	/// @warning The names point into NGX memory, use them before the next NGX call
	[[nodiscard]]
	static auto instanceExtensions() -> std::vector<const char*>;

	/// What NGX needs on this NVIDIA device, empty when the query fails. Same lifetime as instanceExtensions()
	[[nodiscard]]
	static auto deviceExtensions(vk::Instance instance, vk::PhysicalDevice physical_device) -> std::vector<const char*>;

	explicit DlssUpscaler(const VulkanCore& core);
	/// Must run before the device is destroyed
	~DlssUpscaler();

	DlssUpscaler(const DlssUpscaler&) = delete;
	auto operator=(const DlssUpscaler&) -> DlssUpscaler& = delete;

	[[nodiscard]]
	auto available() const noexcept -> bool {
		return m_available;
	}

	/// Why DLSS is off or failing, empty when it works. By value since the render thread writes it
	[[nodiscard]]
	auto failureReason() const -> std::string {
		const std::scoped_lock lock(m_runtime_mutex);
		return m_runtime_reason.empty() ? m_failure_reason : m_runtime_reason;
	}

	/// Records a failure after init, cleared by a successful evaluate or releaseFeature
	void setRuntimeReason(std::string reason) {
		const std::scoped_lock lock(m_runtime_mutex);
		m_runtime_reason = std::move(reason);
	}

	/// Creates the feature on first use, never recreates a live one since only an idle device may release it
	void ensureFeature(vk::CommandBuffer cmd, vk::Extent2D render_extent, vk::Extent2D output_extent);

	/// False when nothing usable was recorded and the caller resolves the frame itself
	[[nodiscard]]
	auto evaluate(vk::CommandBuffer cmd, const DlssInputs& inputs) -> bool;

	/// Keeps NGX initialised. The device must be idle
	void releaseFeature() noexcept;

private:
	/// Empty on success, otherwise why DLSS is off
	[[nodiscard]]
	auto initialize() -> std::string;
	void release() noexcept;

	const VulkanCore* m_core = nullptr;
	NVSDK_NGX_Parameter* m_parameters = nullptr;
	NVSDK_NGX_Handle* m_feature = nullptr;
	vk::Extent2D m_feature_render {};
	vk::Extent2D m_feature_output {};
	/// Creation failed for the current extents, retried after the next releaseFeature
	bool m_create_failed = false;
	/// The first evaluate of a new feature has no history
	bool m_reset_pending = false;
	bool m_mismatch_logged = false;
	bool m_evaluate_failure_logged = false;
	bool m_ngx_initialized = false;

	bool m_available = false;
	std::string m_failure_reason;
	/// Read by the UI thread, written by the render thread
	mutable std::mutex m_runtime_mutex;
	std::string m_runtime_reason;
};

}
