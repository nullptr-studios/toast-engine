/**
 * @file material_pass.hpp
 * @author Xein
 * @date 17 Jul 2026
 */

#pragma once

#include "../material_runtime.hpp"
#include "../render_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_pipeline.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <toast/assets/material.hpp>
#include <unordered_map>
#include <vector>

namespace renderer {
class VulkanCore;

/**
 * @class MaterialPass
 * @brief Draws every mesh instance whose root material owns this pass
 */
class MaterialPass : public IRenderPass {
public:
	MaterialPass(
	    const VulkanCore& core, assets::Material* root_material, vk::Format color_format, vk::Format depth_format,
	    vk::Extent2D extent
	);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return m_name;
	}

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	[[nodiscard]]
	auto rootMaterial() const -> assets::Material* {
		return m_root_material;
	}

	/// Schedules a full pipeline + descriptor rebuild at the start of the next record()
	void markShadersDirty() { m_rebuild_pending.store(true, std::memory_order_release); }

	/// Schedules a re-bake of parameter values
	void markValuesDirty() { m_values_dirty.store(true, std::memory_order_release); }

	[[nodiscard]]
	auto isValid() const -> bool {
		return m_pipeline.isReady();
	}

private:
	/// Per material GPU resources within this pass
	struct InstanceResources {
		std::unique_ptr<MaterialRuntime> runtime;

		struct UboBuffer {
			uint32_t set = 0;
			uint32_t binding = 0;
			std::vector<std::optional<vma::raii::Buffer>> buffers;
		};

		std::vector<UboBuffer> ubo_buffers;
		std::vector<std::vector<vk::raii::DescriptorSet>> sets;
		std::vector<std::vector<vk::ImageView>> bound_views;
	};

	void rebuildPipeline();
	auto ensureInstanceResources(assets::Material* material) -> InstanceResources*;
	void updateInstanceDescriptors(InstanceResources& res, uint32_t frame_index);
	void createFrameSets();

	const VulkanCore* m_core = nullptr;
	assets::Material* m_root_material = nullptr;
	std::string m_name;

	vk::Format m_color_format = vk::Format::eUndefined;
	vk::Format m_depth_format = vk::Format::eUndefined;
	vk::Extent2D m_extent {};

	MaterialRuntime m_root_runtime;
	ShaderLayout m_layout;
	VulkanPipeline m_pipeline;

	std::vector<vk::raii::DescriptorSet> m_frame_descriptor_sets;
	std::unordered_map<assets::Material*, InstanceResources> m_instances;

	std::atomic_bool m_rebuild_pending {false};
	std::atomic_bool m_values_dirty {false};
};

}
