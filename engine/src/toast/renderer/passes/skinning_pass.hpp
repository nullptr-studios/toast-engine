/**
 * @file skinning_pass.hpp
 * @author dario
 * @date 13/08/2026
 */

#pragma once

#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace renderer {
class VulkanCore;
class VulkanMesh;

/// @note Output is world space so posed proxies use an identity model matrix
class SkinningPass {
public:
	explicit SkinningPass(const VulkanCore& core);

	void record(vk::CommandBuffer cmd, uint32_t frame_index);

	[[nodiscard]]
	auto getPosedVertexBuffer(uint32_t frame_index) const -> vk::Buffer;

	[[nodiscard]]
	auto getPosedInstanceCount() const noexcept -> uint32_t {
		return m_posed_instances;
	}

	[[nodiscard]]
	auto isReady() const noexcept -> bool {
		return m_pipeline.isReady();
	}

	static constexpr uint32_t k_max_posed_vertices = 1u << 19;

private:
	/// Mirrors skinning.slang PushConstants
	struct PushConstants {
		uint32_t vertex_count = 0;
		uint32_t output_base = 0;
		uint32_t joint_offset = 0;
		uint32_t pad0 = 0;
	};

	struct MeshResources {
		std::vector<vk::raii::DescriptorSet> sets;
	};

	auto ensureMeshResources(const VulkanMesh* mesh) -> MeshResources*;

	const VulkanCore* m_core = nullptr;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_pipeline;

	std::vector<std::optional<vma::raii::Buffer>> m_posed_buffers;
	std::unordered_map<const VulkanMesh*, MeshResources> m_mesh_resources;

	uint32_t m_posed_instances = 0;
};

}
