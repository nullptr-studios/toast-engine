/**
 * @file material_pass.hpp
 * @author Xein
 * @date 17 Jul 2026
 */

#pragma once

#include "../material_runtime.hpp"
#include "../render_pass_base.hpp"
#include "../scene_descriptor_set.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_pipeline.hpp"
#include "../vulkan_renderer.hpp"

#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <toast/assets/material.hpp>
#include <unordered_map>
#include <vector>

namespace renderer {
class VulkanCore;

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

	~MaterialPass() override;

	MaterialPass(const MaterialPass&) = delete;
	auto operator=(const MaterialPass&) -> MaterialPass& = delete;
	MaterialPass(MaterialPass&&) = delete;
	auto operator=(MaterialPass&&) -> MaterialPass& = delete;

	void update(uint32_t frame_index, float dt) override;

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	void recordInstance(vk::CommandBuffer cmd, uint32_t frame_index, const VulkanRenderer::MeshInstanceProxy& proxy);

	/// Render thread and blocks on a running build for frames such as probe captures
	void finishPendingBuild();

	[[nodiscard]]
	auto rootMaterial() const -> assets::Material* {
		return m_root_material;
	}

	/// Any thread and the old pipeline draws until the new one is built
	void markShadersDirty() { m_rebuild_pending.store(true, std::memory_order_release); }

	void markValuesDirty() { m_values_dirty.store(true, std::memory_order_release); }

	[[nodiscard]]
	auto isValid() const -> bool {
		return m_built != nullptr;
	}

	[[nodiscard]]
	auto usesCutout() const noexcept -> bool {
		return m_built != nullptr && m_built->uses_cutout;
	}

	[[nodiscard]]
	auto isBlended() const -> bool {
		return m_root_material != nullptr && m_root_material->settings().blend_mode != assets::BlendMode::opaque;
	}

private:
	[[nodiscard]]
	static auto resolvedAlphaCutoffOf(MaterialRuntime& runtime) -> float;

	struct InstanceResources {
		std::unique_ptr<MaterialRuntime> runtime;

		struct UboBuffer {
			uint32_t set = 0;
			uint32_t binding = 0;
			std::vector<std::optional<vma::raii::Buffer>> buffers;
		};

		std::vector<UboBuffer> ubo_buffers;
		std::vector<std::vector<vk::raii::DescriptorSet>> sets;

		/// View plus texture generation since a recreated view can reuse a destroyed handle value
		struct BoundView {
			vk::ImageView view;
			uint64_t generation = 0;

			auto operator==(const BoundView&) const -> bool = default;
		};

		std::vector<std::vector<BoundView>> bound_views;
	};

	struct Built {
		Built(const VulkanCore& core, assets::Material* material) : runtime(core, material) { }

		MaterialRuntime runtime;
		ShaderLayout layout;
		VulkanPipeline pipeline;
		bool uses_cutout = false;

		/// Render thread only since they come from the renderer descriptor pool
		SceneDescriptorSets scene_sets;
		std::unordered_map<assets::Material*, InstanceResources> instances;
	};

	/// Inputs copied so a worker never touches the pass
	struct BuildInputs {
		const VulkanCore* core = nullptr;
		assets::Material* root_material = nullptr;
		std::string name;
		vk::Format color_format = vk::Format::eUndefined;
		vk::Format depth_format = vk::Format::eUndefined;
		vk::Extent2D extent;
	};

	/// Worker thread and null when the shaders or pipeline failed
	static auto build(const BuildInputs& inputs) -> std::shared_ptr<Built>;
	void startBuild();
	void adopt(std::shared_ptr<Built> built);

	void drawInstance(
	    Built& built, vk::CommandBuffer cmd, uint32_t frame_index, const VulkanRenderer::MeshInstanceProxy& proxy,
	    vk::Buffer posed_vertices, uint32_t posed_vertex_offset, assets::Material** bound_material, uint32_t instance_count = 1
	);

	auto ensureInstanceResources(Built& built, assets::Material* material) -> InstanceResources*;
	void updateInstanceDescriptors(InstanceResources& res, uint32_t frame_index);

	const VulkanCore* m_core = nullptr;
	assets::Material* m_root_material = nullptr;

	assets::Handle<assets::Material> m_root_material_ref;
	std::string m_name;

	vk::Format m_color_format = vk::Format::eUndefined;
	vk::Format m_depth_format = vk::Format::eUndefined;
	vk::Extent2D m_extent;

	/// Render thread only
	std::shared_ptr<Built> m_built;
	std::future<std::shared_ptr<Built>> m_pending_build;

	std::vector<std::byte> m_push_scratch;
	std::vector<uint32_t> m_draw_order;

	std::atomic_bool m_rebuild_pending {false};
	std::atomic_bool m_values_dirty {false};
};

}
