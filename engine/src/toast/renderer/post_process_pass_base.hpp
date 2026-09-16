/// @file post_process_pass_base.hpp
/// @author dario
/// @date 03/08/2026

#pragma once

#include "vulkan_common.hpp"

#include <atomic>
#include <string_view>

namespace renderer {

class IPostProcessPass {
public:
	virtual ~IPostProcessPass() = default;

	/// @returns the next pass input. Return @p source_view to leave the image unchanged
	virtual auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView = 0;

	virtual void onResize(vk::Extent2D extent) { }

	[[nodiscard]]
	virtual auto name() const -> std::string_view {
		return "PostProcess";
	}

	void setEnabled(bool enabled) noexcept { m_enabled.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto isEnabled() const noexcept -> bool {
		return m_enabled.load(std::memory_order_relaxed);
	}

private:
	std::atomic_bool m_enabled {true};
};

}
