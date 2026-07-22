/**
 * @file ui_pass.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Viewport-space UI render pass
 */

#pragma once
#include <array>
#include <memory>
#include <optional>
#include <toast/renderer/render_pass_base.hpp>
#include <toast/renderer/shader_layout.hpp>
#include <toast/renderer/vulkan_common.hpp>
#include <toast/renderer/vulkan_pipeline.hpp>
#include <vector>

namespace renderer {
class VulkanCore;
}

namespace ui {

class UIPass : public IRenderPass {
public:
	/// UI layers always render in this format regardless of the output target
	static constexpr vk::Format k_color_format = vk::Format::eR8G8B8A8Unorm;

	[[nodiscard]]
	static auto selectStencilFormat(const renderer::VulkanCore& core) -> vk::Format;

	UIPass(const renderer::VulkanCore& core, vk::Format output_color_format, vk::Format output_depth_format, vk::Extent2D extent);

	auto name() const -> std::string_view override { return "UI"; }

	void recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;
	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

private:
	const renderer::VulkanCore* m_core = nullptr;

	vk::raii::Sampler m_sampler = nullptr;
	renderer::ShaderLayout m_shader_layout;
	renderer::VulkanPipeline m_composite_pipeline;
	std::array<std::vector<vk::raii::DescriptorSet>, 3> m_frame_sets;
	uint32_t m_draw_count = 0;

	// Keeps the command pool slot of the UI buffers alive per frame in flight
	std::array<std::shared_ptr<const void>, 3> m_executing_guards;
};

}
