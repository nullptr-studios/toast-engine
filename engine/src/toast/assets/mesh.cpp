#include "mesh.hpp"

#include "toast/renderer/vulkan_renderer.hpp"

namespace assets {

Mesh::Mesh(const std::vector<uint8_t>& data) {
	TOAST_ASSERT(data.size() >= sizeof(_detail::MeshFileHeader), "AssetManager", "Mesh data is too small to contain header");

	_detail::MeshFileHeader header;
	std::array<uint8_t, 6> cmp_magic = {'T', 'M', 'E', 'S', 'H', '\0'};
	std::memcpy(static_cast<void*>(&header), data.data(), sizeof(_detail::MeshFileHeader));
	TOAST_ASSERT(header.magic == cmp_magic, "AssetManager", "Mesh data has invalid magic");

	switch (header.version) {
		case 1: {
			TOAST_ASSERT(
			    data.size() >= sizeof(_detail::MeshFileHeader) + sizeof(uint8_t), "AssetManager", "Mesh data too small for name length"
			);

			const uint8_t* data_start = data.data() + sizeof(header);

			// name
			uint8_t name_length = 0;
			memcpy(&name_length, data_start, sizeof(name_length));

			size_t expected_size = sizeof(_detail::MeshFileHeader) + sizeof(uint8_t) + name_length +
			                       (header.vertex_count * sizeof(toast::renderer::Vertex)) + (header.index_count * sizeof(uint32_t));

			TOAST_ASSERT(
			    data.size() == expected_size, "AssetManager", "Mesh data size does not match expected size based on header information"
			);
			data_start += sizeof(name_length);

			m_name.resize(name_length);
			memcpy(m_name.data(), data_start, name_length);
			data_start += name_length;

			// Reserve sizes
			m_vertices.resize(header.vertex_count);
			m_indices.resize(header.index_count);

			// Import sizes
			// clang-format off
			memcpy(
					m_vertices.data(),
					data_start,
					header.vertex_count * sizeof(toast::renderer::Vertex)
			);
			data_start += header.vertex_count * sizeof(toast::renderer::Vertex);

			memcpy(
					m_indices.data(),
					data_start,
					header.index_count * sizeof(uint32_t)
			);
			// clang-format on
			break;
		}
		default: TOAST_ASSERT(false, "AssetManager", "Mesh data has invalid version");
	}

	// create GPU Side mesh
	toast::renderer::VulkanRenderer::instance->queueResourceUpload(
	    std::make_unique<toast::renderer::MeshUpload>(
	        m_gpu_mesh, toast::renderer::VulkanMesh::UploadData {m_vertices, m_indices}, m_name
	    )
	);
}

auto Mesh::toBinary() const -> std::vector<uint8_t> {
	std::vector<uint8_t> buffer;

	// File header
	_detail::MeshFileHeader header;
	header.vertex_count = static_cast<uint32_t>(m_vertices.size());
	header.index_count = static_cast<uint32_t>(m_indices.size());
	const uint8_t* header_start = reinterpret_cast<const uint8_t*>(&header);
	buffer.insert(buffer.end(), header_start, header_start + sizeof(header));

	// name
	uint8_t name_length = static_cast<uint8_t>(std::min(m_name.size(), static_cast<size_t>(255)));
	buffer.insert(buffer.end(), &name_length, &name_length + sizeof(name_length));
	buffer.insert(buffer.end(), m_name.begin(), m_name.begin() + name_length);

	// vectors
	const uint8_t* vertices_start = reinterpret_cast<const uint8_t*>(m_vertices.data());
	buffer.insert(buffer.end(), vertices_start, vertices_start + (sizeof(toast::renderer::Vertex) * m_vertices.size()));
	const uint8_t* indices_start = reinterpret_cast<const uint8_t*>(m_indices.data());
	buffer.insert(buffer.end(), indices_start, indices_start + (sizeof(uint32_t) * m_indices.size()));

	return buffer;
}
}
