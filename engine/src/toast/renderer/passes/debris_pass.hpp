/**
 * @file debris_pass.hpp
 * @author Xein
 * @date 19 Oct 2026
 */

#pragma once

#include "../render_pass_base.hpp"
#include "../scene_descriptor_set.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <memory>
#include <string_view>
#include <vector>

namespace physics {
class DebrisSimulation;
}

namespace renderer {
class VulkanCore;

class DebrisPass : public IRenderPass {
public:
	DebrisPass(
	    const VulkanCore& core, vk::Format depth_format, vk::Extent2D extent, std::shared_ptr<physics::DebrisSimulation> simulation
	);

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return RenderStage::voxel_gbuffer;
	}

	[[nodiscard]]
	auto prepare(vk::CommandBuffer cmd, uint32_t frame_index) -> bool;

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Debris";
	}

private:
	struct PushConstants {
		float extrapolation = 0.0f;
		float frame_delta = 0.0f;
		float voxel_size = 0.0f;
		uint32_t list_capacity = 0;
	};

	const VulkanCore* m_core = nullptr;
	std::shared_ptr<physics::DebrisSimulation> m_simulation;

	ShaderLayout m_layout;
	VulkanPipeline m_pipeline;
	SceneDescriptorSets m_scene_sets;
	std::vector<vk::raii::DescriptorSet> m_sets;
	std::vector<std::array<vk::Buffer, 2>> m_bound;
	vk::Buffer m_args = nullptr;
	PushConstants m_push;
};

}
