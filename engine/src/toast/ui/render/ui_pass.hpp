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

	void recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;
	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

private:
	struct Target {
		std::optional<vma::raii::Image> color_image;
		vk::raii::ImageView color_view = nullptr;
		std::optional<vma::raii::Image> stencil_image;
		vk::raii::ImageView stencil_view = nullptr;
		vk::Extent2D extent {};
	};

	void createTarget(vk::Extent2D extent);
	void writeDescriptor();

	const renderer::VulkanCore* m_core = nullptr;
	vk::Format m_stencil_format = vk::Format::eUndefined;

	Target m_target;

	// Old targets stay alive until every frame in flight that might sample them has cycled
	struct RetiredTarget {
		Target target;
		uint32_t frames_left = 0;
	};

	std::vector<RetiredTarget> m_retired_targets;

	vk::raii::Sampler m_sampler = nullptr;
	renderer::ShaderLayout m_shader_layout;
	renderer::VulkanPipeline m_composite_pipeline;
	vk::raii::DescriptorSet m_descriptor_set = nullptr;

	bool m_has_content = false;

	// Keeps the command pool slot of the UI buffers alive per frame in flight
	std::array<std::shared_ptr<const void>, 3> m_executing_guards;
};

}
