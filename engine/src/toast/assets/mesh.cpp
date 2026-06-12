#include "mesh.hpp"

namespace assets {

Mesh::Mesh(const std::vector<uint8_t>& data) {
	TOAST_ASSERT(data.size() >= sizeof(_detail::MeshFileHeader), "AssetManager", "Mesh data is too small to contain header");

	_detail::MeshFileHeader header;
	std::array<uint8_t, 6> cmp_magic = {'T', 'M', 'E', 'S', 'H', '\0'};
	std::memcpy(static_cast<void*>(&header), data.data(), sizeof(_detail::MeshFileHeader));
	TOAST_ASSERT(header.magic == cmp_magic, "AssetManager", "Mesh data has invalid magic");

	switch (header.version) {
		case 1: {
			size_t expected_size =
			    sizeof(_detail::MeshFileHeader) + header.vertex_count * sizeof(Vertex) + header.index_count * sizeof(uint32_t);
			TOAST_ASSERT(
			    data.size() == expected_size, "AssetManager", "Mesh data size does not match expected size based on header information"
			);

			// Reserve sizes
			m_vertices.resize(header.vertex_count);
			m_indices.resize(header.index_count);

			// Import sizes
			// clang-format off
				memcpy(
				    m_vertices.data(),
				    data.data() + sizeof(header),
				    header.vertex_count * sizeof(Vertex)
				);
				memcpy(
				    m_indices.data(),
				    data.data() + sizeof(header) + header.vertex_count * sizeof(Vertex),
				    header.index_count * sizeof(uint32_t)
				);
			// clang-format on
			break;
		}
		default: TOAST_ASSERT(false, "AssetManager", "Mesh data has invalid version");
	}
}

auto Mesh::toBinary() const {
	std::vector<uint8_t> buffer;

	// File header
	_detail::MeshFileHeader header;
	header.vertex_count = static_cast<uint32_t>(m_vertices.size());
	header.index_count = static_cast<uint32_t>(m_indices.size());
	const uint8_t* header_start = header.magic.data();
	buffer.insert(buffer.end(), header_start, header_start + sizeof(header));

	// vectors
	const uint8_t* vertices_start = reinterpret_cast<const uint8_t*>(m_vertices.data());
	buffer.insert(buffer.end(), vertices_start, vertices_start + sizeof(m_vertices));
	const uint8_t* indices_start = reinterpret_cast<const uint8_t*>(m_indices.data());
	buffer.insert(buffer.end(), indices_start, indices_start + sizeof(m_vertices));
}
}
