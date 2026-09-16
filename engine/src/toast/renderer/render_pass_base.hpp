/// @file IRenderPass.hpp
/// @author dario
/// @date 07/06/2026

#pragma once

#include "vulkan_common.hpp"

#include <atomic>
#include <string_view>

enum class RenderStage : uint8_t {
	world,
	overlay,
};

class IRenderPass {
public:
	virtual ~IRenderPass() = default;

	[[nodiscard]]
	virtual auto stage() const -> RenderStage {
		return RenderStage::world;
	}

	virtual void update(uint32_t frame_index, float dt) { }

	/// @brief Records outside any rendering scope before the main scope opens
	virtual void recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) { }

	virtual void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) = 0;

	[[nodiscard]]
	virtual auto name() const -> std::string_view {
		return "Pass";
	}

	void setEnabled(bool enabled) noexcept { m_enabled.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto isEnabled() const noexcept -> bool {
		return m_enabled.load(std::memory_order_relaxed);
	}

private:
	std::atomic_bool m_enabled {true};
};
