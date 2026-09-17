/**
 * @file voxel_pass.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once
#include "../render_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <string_view>
#include <vector>

namespace renderer {
class VulkanCore;
class VoxelGpuStorage;

class VoxelPass : public IRenderPass {
public:
	VoxelPass(const VulkanCore& core, vk::Format scene_format, vk::Format depth_format, vk::Extent2D extent);

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return RenderStage::world;
	}

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Voxels";
	}

	static constexpr uint32_t k_max_instances = 4096;

private:
	/// Mirrors voxel.slang PushConstants
	struct PushConstants {
		uint32_t instance_index = 0;
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
		uint32_t pad2 = 0;
	};

	/// Mirrors voxel_dda.slang VoxelInstance
	struct InstanceGpu {
		glm::mat4 voxel_to_world {1.0f};
		glm::mat4 world_to_voxel {1.0f};
		uint32_t record_index = 0;
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
		uint32_t pad2 = 0;
	};

	enum class Cull : uint8_t {
		back,
		front,
	};

	enum class Depth : uint8_t {
		/// SV_DepthGreaterEqual with the camera outside
		conservative,
		/// SV_Depth with the camera inside
		exact,
	};

	struct Draw {
		uint32_t instance = 0;
		Cull cull = Cull::back;
		Depth depth = Depth::conservative;
	};

	void createInstanceBuffers(const VulkanCore& core);
	void createDescriptors(const VulkanCore& core);

	void bindStorage(uint32_t frame_index, const std::shared_ptr<const VoxelGpuStorage>& storage);

	const VulkanCore* m_core = nullptr;
	ShaderLayout m_shader_layout;

	/// Indexed [cull][depth]
	std::array<std::array<VulkanPipeline, 2>, 2> m_pipelines;

	std::vector<vk::raii::DescriptorSet> m_camera_sets;

	std::vector<vk::raii::DescriptorSet> m_scene_sets;

	std::vector<vma::raii::Buffer> m_instance_buffers;

	std::vector<std::shared_ptr<const VoxelGpuStorage>> m_bound_storage;

	std::vector<Draw> m_draws;
};

}
