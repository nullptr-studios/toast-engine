/**
 * @file debris_pass.hpp
 * @author Xein
 * @date 19 Oct 2026
 */

#pragma once

#include "toast/renderer/core/descriptor_cache.hpp"
#include "toast/renderer/passes/render_pass_base.hpp"
#include "toast/renderer/resources/scene_descriptor_set.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
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
	    const VulkanCore& core, vk::Format scene_format, vk::Format depth_format, vk::Extent2D extent,
	    std::shared_ptr<physics::DebrisSimulation> simulation
	);

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return RenderStage::voxel_gbuffer;
	}

	[[nodiscard]]
	auto prepare(vk::CommandBuffer cmd, uint32_t frame_index) -> bool;

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	/// World scope with the blended draws, after record() in the same frame
	void recordGlass(vk::CommandBuffer cmd, uint32_t frame_index);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Debris";
	}

	/// What the shadow pass needs to draw the opaque debris, args is null unless prepare() succeeded this frame
	struct ShadowDraw {
		vk::DescriptorSet set = nullptr;
		vk::Buffer args = nullptr;
		float extrapolation = 0.0f;
		float voxel_size = 0.0f;
		uint32_t list_capacity = 0;
		/// Sphere around every opaque piece as drawn, radius below 0 when there are none
		glm::vec3 bounds_center {0.0f};
		float bounds_radius = -1.0f;
		/// Empty while a piece moves so its shadow must redraw, otherwise stable until one changes
		std::optional<uint64_t> signature;
	};

	[[nodiscard]]
	auto shadowDraw(uint32_t frame_index) const -> ShadowDraw {
		if (!m_args || frame_index >= m_sets.size()) {
			return {};
		}
		ShadowDraw draw = m_shadow;
		draw.set = *m_sets[frame_index];
		draw.args = m_args;
		return draw;
	}

private:
	struct PushConstants {
		float extrapolation = 0.0f;
		float frame_delta = 0.0f;
		float voxel_size = 0.0f;
		uint32_t list_capacity = 0;
		float reflection_reach = 0.0f;
		uint32_t reflection_steps = 0;
		float reflection_thickness = 0.0f;
		float reflection_roughness = 0.0f;
	};

	const VulkanCore* m_core = nullptr;
	std::shared_ptr<physics::DebrisSimulation> m_simulation;

	ShaderLayout m_layout;
	VulkanPipeline m_pipeline;
	VulkanPipeline m_glass_pipeline;
	SceneDescriptorSets m_scene_sets;
	std::vector<vk::raii::DescriptorSet> m_sets;
	DescriptorCache<std::array<vk::Buffer, 2>> m_bound;
	vk::Buffer m_args = nullptr;
	PushConstants m_push;
	/// Glass backdrop colour and depth the SSR march of glass pieces reads
	std::vector<vk::raii::DescriptorSet> m_backdrop_sets;
	DescriptorCache<std::array<vk::ImageView, 2>> m_bound_backdrop;
	vk::raii::Sampler m_backdrop_sampler = nullptr;
	vk::raii::Sampler m_backdrop_depth_sampler = nullptr;
	/// Without the set and args which come per frame
	ShadowDraw m_shadow;
};

}
