/// @file VulkanRenderer.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "output_target.hpp"
#include "vulkan_core.hpp"
#include "vulkan_pipeline.hpp"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace toast::renderer {

/**
 * @brief Manages rendering operations and graphics resources using the Vulkan API.
 *
 * The VulkanRenderer class encapsulates the initialization of the graphics
 * pipeline and handles the lifecycle of render resources such as
 * swap chains, framebuffers, and command buffers.
 *
 * @details
 *
 * This class coordinates the rendering process by abstracting the
 * underlying Vulkan object handles. It ensures proper synchronization
 * between rendering threads and frame presentation operations.
 */
class VulkanRenderer {
public:
	static auto selectDepthFormat(const VulkanCore& core) -> vk::Format;

	struct FrameUniformData {
		std::array<float, 2> i_resolution;
		float i_time;
		float padding;
	};

	struct FrameContext {
		vk::raii::CommandBuffer command_buffer = nullptr;
		vk::raii::CommandBuffer transfer_command_buffer = nullptr;
		vk::raii::Semaphore image_available = nullptr;
		vk::raii::Semaphore transfer_finished = nullptr;
		vk::raii::Fence in_flight = nullptr;
		uint32_t last_image_index = 0;
		bool has_submitted = false;
	};

	VulkanRenderer(
	    const VulkanCore& core, std::unique_ptr<IOutputTarget> output_target, std::unique_ptr<VulkanPipeline> pipeline,
	    uint32_t frames_in_flight = 3
	);

	~VulkanRenderer();

	VulkanRenderer(const VulkanRenderer&) = delete;
	auto operator=(const VulkanRenderer&) -> VulkanRenderer& = delete;
	VulkanRenderer(VulkanRenderer&&) = delete;
	auto operator=(VulkanRenderer&&) -> VulkanRenderer& = delete;

	void drawFrame();

	void resize(vk::Extent2D extent);

	[[nodiscard]]
	auto getOutputTarget() const -> const IOutputTarget& {
		return *m_output_target;
	}

	[[nodiscard]]
	auto getOutputTarget() -> IOutputTarget& {
		return *m_output_target;
	}

private:
	struct DepthResources {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
	};

	struct FrameUniformResources {
		std::optional<vma::raii::Buffer> staging_buffer;
		std::optional<vma::raii::Buffer> gpu_buffer;
		vk::DescriptorSet descriptor_set = nullptr;
	};

	void createGraphicsCommandPool();
	void createTransferCommandPool();
	void createFrameContexts(uint32_t frames_in_flight);
	void createPerImageSync();
	void createDepthResources();

	void createFrameUniformResources();
	void updateFrameUniformData();

	void recordTransferPass(FrameContext& frame);

	void recordComputePass(FrameContext& frame, uint32_t image_index);

	void recordFrame(FrameContext& frame, uint32_t image_index);

	const VulkanCore* m_core = nullptr;

	std::unique_ptr<IOutputTarget> m_output_target;
	std::unique_ptr<VulkanPipeline> m_pipeline;
	vk::Format m_depth_format = vk::Format::eUndefined;
	DepthResources m_depth_resources;
	FrameUniformData m_frame_uniform_data {};    // DEBUGGING
	FrameUniformResources m_frame_uniform_resources;
	vk::ImageLayout m_depth_layout = vk::ImageLayout::eUndefined;

	vk::raii::CommandPool m_command_pool = nullptr;

	vk::raii::CommandPool m_transfer_command_pool = nullptr;

	vk::raii::DescriptorPool m_descriptor_pool = nullptr;

	std::vector<FrameContext> m_frames;
	std::vector<vk::raii::Semaphore> m_render_finished_per_image;
	std::vector<vk::Fence> m_images_in_flight;
	uint32_t m_current_frame = 0;

	// DEBUGGING
	float m_total_time = 0.0f;
};

}
