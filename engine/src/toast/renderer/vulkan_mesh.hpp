/// @file VulkanMesh.hpp
/// @author dario
/// @date 07/06/2026

#pragma once

#include "glm/glm.hpp"
#include "vertex.hpp"
#include "vulkan_common.hpp"
#include "vulkan_resource_base.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <toast/assets/core_types.hpp>

namespace renderer {
class VulkanCore;

auto vertexBindingDescription() -> vk::VertexInputBindingDescription;

auto vertexAttributeDescriptions() -> std::array<vk::VertexInputAttributeDescription, 5>;

class VulkanMesh : public IVulkanResource {
public:
	VulkanMesh() = default;

	struct UploadData {
		std::span<const Vertex> vertices;
		std::span<const uint32_t> indices;
		std::span<const SkinVertex> skin_vertices;
	};

	void create(
	    const renderer::VulkanCore& core, UploadData data, uint32_t graphics_queue_family_index,
	    uint32_t transfer_queue_family_index, std::string_view debug_name = {}
	);

	void destroy();

	void bind(vk::CommandBuffer cmd) const;

	void bindPosed(vk::CommandBuffer cmd, vk::Buffer posed_vertices, uint32_t posed_vertex_offset) const;

	[[nodiscard]]
	auto getVertexCount() const noexcept -> uint32_t {
		return m_vertex_count;
	}

	[[nodiscard]]
	auto getVertexBuffer() const -> vk::Buffer {
		return m_vertex_buffer.has_value() ? **m_vertex_buffer : vk::Buffer {};
	}

	[[nodiscard]]
	auto getSkinVertexBuffer() const -> vk::Buffer {
		return m_skin_vertex_buffer.has_value() ? **m_skin_vertex_buffer : vk::Buffer {};
	}

	[[nodiscard]]
	auto getIndexBuffer() const -> vk::Buffer {
		return m_index_buffer.has_value() ? **m_index_buffer : vk::Buffer {};
	}

	[[nodiscard]]
	auto getIndexCount() const noexcept -> uint32_t {
		return m_index_count;
	}

	void draw(vk::CommandBuffer cmd, uint32_t instance_count = 1) const;

	[[nodiscard]]
	auto isSkinned() const noexcept -> bool {
		return m_skin_vertex_buffer.has_value();
	}

	void recordUpload(
	    vk::CommandBuffer cmd, vk::Buffer staging_buffer, vk::DeviceSize vertex_offset, vk::DeviceSize index_offset,
	    vk::DeviceSize skin_vertex_offset
	) const;

	auto createAccelerationStructure(const VulkanCore& core) -> std::optional<vma::raii::Buffer>;

	void recordBuildAccelerationStructure(vk::CommandBuffer cmd) const;

	[[nodiscard]]
	auto hasAccelerationStructure() const noexcept -> bool {
		return m_blas_address != 0;
	}

	[[nodiscard]]
	auto getAccelerationStructureAddress() const noexcept -> vk::DeviceAddress {
		return m_blas_address;
	}

private:
	std::optional<vma::raii::Buffer> m_vertex_buffer;
	std::optional<vma::raii::Buffer> m_index_buffer;
	std::optional<vma::raii::Buffer> m_skin_vertex_buffer;

	vk::DeviceSize m_vertex_size = 0;
	vk::DeviceSize m_index_size = 0;
	vk::DeviceSize m_skin_vertex_size = 0;

	uint32_t m_vertex_count = 0;
	uint32_t m_index_count = 0;

	std::optional<vma::raii::Buffer> m_blas_buffer;
	vk::raii::AccelerationStructureKHR m_blas = nullptr;
	vk::DeviceAddress m_blas_address = 0;
	vk::AccelerationStructureBuildGeometryInfoKHR m_blas_build_info {};
	vk::AccelerationStructureGeometryKHR m_blas_geometry {};
	uint32_t m_blas_primitive_count = 0;
	/// Only valid until the build fence signals
	mutable vk::DeviceAddress m_blas_scratch_address = 0;

	/// From the RAII device dispatcher since the vulkan-hpp static loader has no extension entry points
	PFN_vkCmdBuildAccelerationStructuresKHR m_build_acceleration_structures = nullptr;

	friend class MeshUpload;
};

class MeshUpload : public PendingResourceUpload {
public:
	MeshUpload(VulkanMesh& mesh, VulkanMesh::UploadData data, assets::HandleBase source, std::string_view debug_name = {});

	VulkanMesh* mesh;

	VulkanMesh::UploadData data;
	assets::HandleBase source;
	std::string debug_name;

	vma::raii::Buffer vertex_staging = nullptr;

	void build(const VulkanCore& core) override;

	void record(vk::CommandBuffer cmd) override;

	auto resource() -> IVulkanResource* override { return mesh; }
};

}
