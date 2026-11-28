#pragma once
#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/passes/render_pass_base.hpp"

#include <vector>

struct ImGuiContext;

namespace renderer {
class VulkanCore;

class ConsolePass : public IRenderPass {
public:
	ConsolePass(const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format);
	~ConsolePass() override;

	ConsolePass(const ConsolePass&) = delete;
	auto operator=(const ConsolePass&) -> ConsolePass& = delete;

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return RenderStage::overlay;
	}

	void update(uint32_t frame_index, float dt) override;
	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Console";
	}

private:
	ImGuiContext* m_context = nullptr;
	std::vector<uint8_t> m_font_data;
	bool m_ready = false;
	bool m_drawing = false;
};

}
