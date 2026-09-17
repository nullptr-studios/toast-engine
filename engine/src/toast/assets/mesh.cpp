#include "mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <toast/renderer/vulkan_renderer.hpp>
#include <tracy/Tracy.hpp>

namespace assets {

Mesh::Mesh(std::string_view name, std::vector<renderer::Vertex>&& vertices, std::vector<uint32_t>&& indices)
    : m_name(name),
      m_vertices(std::move(vertices)),
      m_indices(std::move(indices)),
      m_gpu_mesh(std::make_unique<renderer::VulkanMesh>()) { }

Mesh::Mesh(
    std::string_view name, std::vector<renderer::Vertex>&& vertices, std::vector<uint32_t>&& indices,
    std::vector<renderer::SkinVertex>&& skin_vertices
)
    : m_name(name),
      m_vertices(std::move(vertices)),
      m_indices(std::move(indices)),
      m_skin_vertices(std::move(skin_vertices)),
      m_gpu_mesh(std::make_unique<renderer::VulkanMesh>()) {
	// A partial skin stream would silently mis-weight the tail of the mesh, so drop it rather than upload
	// something the shader would read past the end of
	if (!m_skin_vertices.empty() && m_skin_vertices.size() != m_vertices.size()) {
		TOAST_ERROR(
		    "AssetManager",
		    "Mesh '{}' has {} skin vertices for {} vertices; discarding the skinning data",
		    m_name,
		    m_skin_vertices.size(),
		    m_vertices.size()
		);
		m_skin_vertices.clear();
	}
}

Mesh::~Mesh() = default;

auto Mesh::gpuMesh() const -> const renderer::VulkanMesh& {
	return *m_gpu_mesh;
}

auto Mesh::gpuMesh() -> renderer::VulkanMesh& {
	return *m_gpu_mesh;
}

void generateTangents(std::vector<renderer::Vertex>& vertices, const std::vector<uint32_t>& indices) {
	ZoneScoped;
	if (vertices.empty() || indices.size() < 3) {
		return;
	}

	std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
	std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0.0f));

	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		const std::array<uint32_t, 3> tri {indices[i], indices[i + 1], indices[i + 2]};
		if (tri[0] >= vertices.size() || tri[1] >= vertices.size() || tri[2] >= vertices.size()) {
			continue;
		}

		const glm::vec3 p0 = vertices[tri[0]].position;
		const glm::vec3 edge1 = glm::vec3(vertices[tri[1]].position) - p0;
		const glm::vec3 edge2 = glm::vec3(vertices[tri[2]].position) - p0;

		const glm::vec2 uv0 = vertices[tri[0]].uv;
		const glm::vec2 duv1 = glm::vec2(vertices[tri[1]].uv) - uv0;
		const glm::vec2 duv2 = glm::vec2(vertices[tri[2]].uv) - uv0;

		// Degenerate UVs (a collapsed triangle, or a mesh with no TEXCOORD_0 at all) have no tangent to
		// contribute; the per-vertex fallback below covers a vertex that ends up with nothing
		const float determinant = (duv1.x * duv2.y) - (duv2.x * duv1.y);
		if (std::abs(determinant) < 1e-12f) {
			continue;
		}

		const float inv_determinant = 1.0f / determinant;
		const glm::vec3 tangent = ((edge1 * duv2.y) - (edge2 * duv1.y)) * inv_determinant;
		const glm::vec3 bitangent = ((edge2 * duv1.x) - (edge1 * duv2.x)) * inv_determinant;

		for (const uint32_t index : tri) {
			tangents[index] += tangent;
			bitangents[index] += bitangent;
		}
	}

	for (size_t v = 0; v < vertices.size(); ++v) {
		const glm::vec3 normal = vertices[v].normal;
		glm::vec3 tangent = tangents[v] - (normal * glm::dot(normal, tangents[v]));

		if (glm::dot(tangent, tangent) < 1e-12f) {
			// No usable accumulation: any vector perpendicular to the normal is as good as another, and
			// baking one keeps every vertex with a valid handedness sign rather than leaving zeros behind
			const glm::vec3 reference = std::abs(normal.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			tangent = glm::cross(reference, normal);
			if (glm::dot(tangent, tangent) < 1e-12f) {
				// Normal itself is degenerate (no NORMAL attribute either) - nothing left to be orthogonal to
				vertices[v].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
				continue;
			}
		}

		tangent = glm::normalize(tangent);
		const float handedness = glm::dot(glm::cross(normal, tangent), bitangents[v]) < 0.0f ? -1.0f : 1.0f;
		vertices[v].tangent = glm::vec4(tangent, handedness);
	}
}

auto Mesh::boundingSphere() const -> const glm::vec4& {
	ZoneScoped;
	if (m_bounding_sphere.has_value()) {
		return *m_bounding_sphere;
	}

	if (m_vertices.empty()) {
		m_bounding_sphere.emplace(0.0f, 0.0f, 0.0f, 0.0f);
		return *m_bounding_sphere;
	}

	// AABB centre rather than the centroid: a mesh with one dense end (a character's head, a tree's canopy)
	// pulls the centroid towards it and inflates the radius needed to still reach the sparse end
	glm::vec3 min_point = m_vertices.front().position;
	glm::vec3 max_point = min_point;
	for (const auto& vertex : m_vertices) {
		min_point = glm::min(min_point, glm::vec3(vertex.position));
		max_point = glm::max(max_point, glm::vec3(vertex.position));
	}

	const glm::vec3 center = (min_point + max_point) * 0.5f;

	float radius_squared = 0.0f;
	for (const auto& vertex : m_vertices) {
		const glm::vec3 offset = glm::vec3(vertex.position) - center;
		radius_squared = std::max(radius_squared, glm::dot(offset, offset));
	}

	// See boundingSphere()'s docs: a skinned mesh is only ever measured in its bind pose here
	constexpr float k_skinned_padding = 1.5f;
	const float radius = std::sqrt(radius_squared) * (isSkinned() ? k_skinned_padding : 1.0f);

	m_bounding_sphere.emplace(center, radius);
	return *m_bounding_sphere;
}

Mesh::Mesh(const std::vector<uint8_t>& data) : m_gpu_mesh(std::make_unique<renderer::VulkanMesh>()) {
	ZoneScoped;
	TOAST_ASSERT(data.size() >= sizeof(_detail::MeshFileHeader), "AssetManager", "Mesh data is too small to contain header");

	_detail::MeshFileHeader header;
	std::array<uint8_t, 6> cmp_magic = {'T', 'M', 'E', 'S', 'H', '\0'};
	std::memcpy(static_cast<void*>(&header), data.data(), sizeof(_detail::MeshFileHeader));
	TOAST_ASSERT(header.magic == cmp_magic, "AssetManager", "Mesh data has invalid magic");

	if (header.version != _detail::mesh_format_version) {
		TOAST_ERROR(
		    "AssetManager",
		    "Mesh uses .tmesh version {} but this build reads {} only; reimport the source asset",
		    header.version,
		    _detail::mesh_format_version
		);
		return;
	}

	TOAST_ASSERT(
	    data.size() >= sizeof(_detail::MeshFileHeader) + sizeof(uint8_t), "AssetManager", "Mesh data too small for name length"
	);

	const uint8_t* data_start = data.data() + sizeof(header);

	// name
	uint8_t name_length = 0;
	memcpy(&name_length, data_start, sizeof(name_length));

	// The trailing uint32 is the skin vertex count, always written even when zero
	const size_t expected_size = sizeof(_detail::MeshFileHeader) + sizeof(uint8_t) + name_length +
	                             (header.vertex_count * sizeof(renderer::Vertex)) + (header.index_count * sizeof(uint32_t)) +
	                             sizeof(uint32_t);

	TOAST_ASSERT(
	    data.size() >= expected_size, "AssetManager", "Mesh data size does not match expected size based on header information"
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
			header.vertex_count * sizeof(renderer::Vertex)
	);
	data_start += header.vertex_count * sizeof(renderer::Vertex);

	memcpy(
			m_indices.data(),
			data_start,
			header.index_count * sizeof(uint32_t)
	);
	data_start += header.index_count * sizeof(uint32_t);
	// clang-format on

	// Skinning block, count always present 0 for a static mesh
	uint32_t skin_vertex_count = 0;
	memcpy(&skin_vertex_count, data_start, sizeof(skin_vertex_count));
	data_start += sizeof(skin_vertex_count);

	if (skin_vertex_count > 0) {
		TOAST_ASSERT(
		    skin_vertex_count == header.vertex_count, "AssetManager", "Mesh skinning block length does not match the vertex count"
		);
		const size_t skin_bytes = static_cast<size_t>(skin_vertex_count) * sizeof(renderer::SkinVertex);
		TOAST_ASSERT(
		    data_start + skin_bytes <= data.data() + data.size(), "AssetManager", "Mesh skinning block runs past end of file"
		);
		m_skin_vertices.resize(skin_vertex_count);
		memcpy(m_skin_vertices.data(), data_start, skin_bytes);
		data_start += skin_bytes;
	}

	// Same guard Texture carries
	if (renderer::VulkanRenderer::instance == nullptr) {
		TOAST_WARN("Mesh", "VulkanRenderer is not available; mesh '{}' was loaded without GPU upload", m_name);
		return;
	}

	// create GPU Side mesh
	// The handle is what keeps this mesh alive until the job build() has read the spans above
	renderer::VulkanRenderer::instance->queueResourceUpload(
	    std::make_unique<renderer::MeshUpload>(
	        *m_gpu_mesh,
	        renderer::VulkanMesh::UploadData {m_vertices, m_indices, m_skin_vertices},
	        assets::HandleBase {this},
	        m_name
	    )
	);
}

auto Mesh::toBinary() const -> std::vector<uint8_t> {
	ZoneScoped;
	std::vector<uint8_t> buffer;

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
	buffer.insert(buffer.end(), vertices_start, vertices_start + (sizeof(renderer::Vertex) * m_vertices.size()));
	const uint8_t* indices_start = reinterpret_cast<const uint8_t*>(m_indices.data());
	buffer.insert(buffer.end(), indices_start, indices_start + (sizeof(uint32_t) * m_indices.size()));

	// v2 skinning block, always written (as a 0 count for static meshes) so the tail is unambiguous
	const auto skin_vertex_count = static_cast<uint32_t>(m_skin_vertices.size());
	const auto* skin_count_start = reinterpret_cast<const uint8_t*>(&skin_vertex_count);
	buffer.insert(buffer.end(), skin_count_start, skin_count_start + sizeof(skin_vertex_count));
	if (skin_vertex_count > 0) {
		const auto* skin_start = reinterpret_cast<const uint8_t*>(m_skin_vertices.data());
		buffer.insert(buffer.end(), skin_start, skin_start + (sizeof(renderer::SkinVertex) * m_skin_vertices.size()));
	}

	return buffer;
}
}
