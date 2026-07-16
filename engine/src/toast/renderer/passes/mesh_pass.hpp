/// @file mesh_pass.hpp
/// @author dario
/// @date 08/06/2026.

#pragma once
#include "../render_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_mesh.hpp"
#include "../vulkan_pipeline.hpp"
#include "../vulkan_texture.hpp"

#include <atomic>
#include <glm/glm.hpp>
#include <toast/events/listener.hpp>
#include <unordered_map>

namespace assets {
class Material;
}

namespace renderer {
class VulkanCore;

/// @brief Render pass for drawing meshes with camera matrices and transformations
class MeshPass : public IRenderPass {
public:
	struct CameraUBO {
		glm::mat4 view;
		glm::mat4 projection;
		glm::mat4 view_projection;
	};

	struct DrawPushConstants {
		glm::mat4 model;
		glm::vec4 color;
	};

	MeshPass(const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent);

	void update(uint32_t frame_index, float dt) override;

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

private:
	void createResources(const renderer::VulkanCore& core);

	/// @brief Builds a 1x1 white fallback texture/sampler/descriptor used for meshes that
	/// have no material or whose material texture isn't uploaded yet
	void createDefaultMaterialResources(const renderer::VulkanCore& core);

	/// @brief Returns the set-1 descriptor set for a material's albedo texture, allocating and/or
	/// rewriting it only when needed
	[[nodiscard]]
	auto getMaterialDescriptorSet(const renderer::VulkanCore& core, assets::Material& material) -> vk::DescriptorSet;

	void updateUBO(uint32_t frame_index);

	renderer::VulkanPipeline m_pipeline;

	renderer::ShaderLayout shader_layout;

	// Set 0: one descriptor set per frame-in-flight, bound to the camera UBO. Owning (vk::raii) so the descriptor sets aren't freed
	// back
	std::vector<vk::raii::DescriptorSet> m_frame_descriptor_sets;

	// per-material descriptor sets keyed by Material
	struct MaterialGpuBinding {
		vk::raii::DescriptorSet set = nullptr;
		vk::ImageView bound_view;
	};

	std::unordered_map<const assets::Material*, MaterialGpuBinding> m_material_sets;

	// Materials are keyed by raw pointer above, but AssetManager::clearUnusedAssets() can destroy a Material
	// whose address then gets reused by an unrelated one. This listener just flags that m_material_sets needs
	// clearing; the actual clear happens on the render thread inside record(), since freeing descriptor sets
	// while the GPU might still be using them is unsafe.
	event::Listener m_asset_listener;
	std::atomic_bool m_pending_material_cache_clear {false};

	// Fallback set 1 used until a mesh's material texture is ready
	renderer::VulkanTexture m_default_texture;
	vk::raii::Sampler m_default_sampler = nullptr;
	vk::raii::DescriptorSet m_default_material_set = nullptr;
};
}
