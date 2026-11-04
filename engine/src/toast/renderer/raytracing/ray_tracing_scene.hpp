/**
 * @file ray_tracing_scene.hpp
 * @author dario
 * @date 13/08/2026
 */

#pragma once

#include "toast/renderer/core/vulkan_common.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace renderer {
class VulkanCore;

class RayTracingScene {
public:
	struct Instance {
		glm::mat4 transform {1.0f};
		vk::DeviceAddress blas_address = 0;
		uint32_t custom_index = 0;
		uint8_t mask = 0xFF;
		bool deforming = false;
	};

	RayTracingScene(const VulkanCore& core, uint32_t frames_in_flight);

	void beginFrame();

	void addInstance(const Instance& instance);

	void build(vk::CommandBuffer cmd, uint32_t frame_index, bool tracing);

	[[nodiscard]]
	auto getAccelerationStructure(uint32_t frame_index) const -> vk::AccelerationStructureKHR;

	/// Bumped whenever the structure of @p frame_index is recreated since the new one can reuse the old handle value
	[[nodiscard]]
	auto getAccelerationStructureGeneration(uint32_t frame_index) const -> uint64_t {
		return frame_index < m_frames.size() ? m_frames[frame_index].tlas_generation : 0;
	}

	[[nodiscard]]
	auto getInstanceCount() const noexcept -> uint32_t {
		return static_cast<uint32_t>(m_instances.size());
	}

	static constexpr uint32_t k_max_instances = 8192;

	static constexpr uint32_t k_max_refits_before_rebuild = 240;

private:
	struct FrameResources {
		std::optional<vma::raii::Buffer> instance_buffer;
		std::optional<vma::raii::Buffer> tlas_buffer;
		std::optional<vma::raii::Buffer> scratch;
		vk::raii::AccelerationStructureKHR tlas = nullptr;
		uint64_t tlas_generation = 0;
		uint32_t built_instances = 0;

		std::vector<Instance> built;
		uint32_t refits_since_build = 0;
	};

	const VulkanCore* m_core = nullptr;
	std::vector<FrameResources> m_frames;
	std::vector<Instance> m_instances;

	/// From the RAII device dispatcher since the vulkan-hpp static loader has no extension entry points
	PFN_vkCmdBuildAccelerationStructuresKHR m_build_acceleration_structures = nullptr;
};

}
