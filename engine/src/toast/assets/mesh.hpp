/**
 * @file mesh.hpp
 * @author Xein
 * @date 11 Jun 2026
 *
 * @brief Represents 3D geometry data, can be read from a .tmesh binary file
 */

#pragma once
#include "core_types.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <toast/export.hpp>
#include <toast/log.hpp>
#include <toast/renderer/vertex.hpp>

namespace renderer {
class VulkanMesh;
}

namespace assets {

namespace _detail {
/**
 * The one .tmesh version this build reads or writes. Older ones are rejected with a reimport message rather
 * than read best-effort, because a mismatched SkinVertex stride misparses instead of failing cleanly
 *
 * Layout: header, uint8 name length, name, vertex_count Vertex, index_count uint32,
 *         uint32 skin vertex count + that many SkinVertex (0 for a static mesh)
 */

inline constexpr uint16_t mesh_format_version = 7;

/**
 * @brief Fixed 16-byte .tmesh header
 *
 * Never gains fields: it is memcpy'd out of the file *before* the version is checked, so growing it shifts
 * the body offset and misparses. New data goes in the body
 */
struct MeshFileHeader {
	std::array<uint8_t, 6> magic = {'T', 'M', 'E', 'S', 'H', '\0'};
	// Defaulted from the constant rather than repeating the number: these two drifted apart once already
	// (writer stamped 5, reader accepted only 4) and every mesh in the project silently failed to load
	uint16_t version = mesh_format_version;
	uint32_t vertex_count = 0;
	uint32_t index_count = 0;
};
}

/**
 * @brief Derives per-vertex tangents from UVs, for geometry that arrived without them
 *
 * glTF expects the client to generate these when a material has no normal map, and exporters take that
 * literally. The shader survives a missing tangent only by inventing a basis, which points any normal map
 * it does have in a meaningless direction
 *
 * The standard accumulate-and-orthonormalize scheme, *not* MikkTSpace - it differs on mirrored UV seams and
 * hard edges, so a mesh baked against MikkTSpace is still better off exporting real tangents
 *
 * @param vertices Modified in place; only the tangent member is written
 * @param indices Triangle list; a trailing partial triangle is ignored
 */
TOAST_API void generateTangents(std::vector<renderer::Vertex>& vertices, const std::vector<uint32_t>& indices);

class TOAST_API Mesh final : public Asset {
public:
	using Index = uint32_t;

	explicit Mesh(const std::vector<uint8_t>& data);
	Mesh(std::string_view name, std::vector<renderer::Vertex>&& vertices, std::vector<uint32_t>&& indices);
	/// @param skin_vertices Per-vertex joint influences; must be empty or exactly as long as @p vertices
	Mesh(
	    std::string_view name, std::vector<renderer::Vertex>&& vertices, std::vector<uint32_t>&& indices,
	    std::vector<renderer::SkinVertex>&& skin_vertices
	);
	~Mesh() override;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "mesh";
	}

	[[nodiscard]]
	auto vertices() const -> const std::vector<renderer::Vertex>& {
		return m_vertices;
	}

	[[nodiscard]]
	auto indices() const -> const std::vector<Index>& {
		return m_indices;
	}

	/// @returns per-vertex joint influences, parallel to vertices(); empty for a static mesh
	[[nodiscard]]
	auto skinVertices() const -> const std::vector<renderer::SkinVertex>& {
		return m_skin_vertices;
	}

	[[nodiscard]]
	auto isSkinned() const noexcept -> bool {
		return !m_skin_vertices.empty();
	}

	/**
	 * @brief Local-space bounding sphere: xyz centre, w radius
	 *
	 * Cached on first use - caster culling asks once per instance per frame, and Bistro's vertex buffers are
	 * too large to walk that often
	 *
	 * Padded generously for skinned meshes, whose sphere covers the bind pose only: a limb can swing outside
	 * it, and a wrongly culled caster is a shadow popping out of existence while a wrongly kept one is a draw
	 */
	[[nodiscard]]
	auto boundingSphere() const -> const glm::vec4&;

	[[nodiscard]]
	auto gpuMesh() const -> const renderer::VulkanMesh&;

	[[nodiscard]]
	auto gpuMesh() -> renderer::VulkanMesh&;

	[[nodiscard]]
	auto name() const -> const std::string& {
		return m_name;
	}

	[[nodiscard]]
	auto toBinary() const -> std::vector<uint8_t>;

private:
	std::string m_name;
	std::vector<renderer::Vertex> m_vertices;
	std::vector<Index> m_indices;
	std::vector<renderer::SkinVertex> m_skin_vertices;

	mutable std::optional<glm::vec4> m_bounding_sphere;

	std::unique_ptr<renderer::VulkanMesh> m_gpu_mesh;
};

}
