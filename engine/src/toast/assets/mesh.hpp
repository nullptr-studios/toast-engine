/**
 * @file mesh.hpp
 * @author Xein
 * @date 11 Jun 2026
 *
 * @brief Represents 3D geometry data, can be read from a .tmesh binary file
 */

#pragma once
#include "core_types.hpp"
#include <toast/log.hpp>
#include <toast/export.hpp>

#include <glm/glm.hpp>

namespace assets {

namespace _detail {
struct MeshFileHeader {
	const std::array<uint8_t, 6> magic = {'T', 'M', 'E', 'S', 'H', '\0'};
	uint16_t version = 1;
	uint32_t vertex_count = 0;
	uint32_t index_count = 0;
};

}

class TOAST_API Mesh final : public Asset {
public:
	explicit Mesh(const std::vector<uint8_t>& data);

	[[nodiscard]]
	auto type() const -> std::string_view override { return "Mesh"; }

	struct Vertex {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec3 tangent;
		glm::vec3 color;
	};

	const std::vector<Vertex>& vertices = m_vertices;
	const std::vector<uint32_t>& indices = m_indices;

private:
	Mesh(std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices)
	    : m_vertices(std::move(vertices)),
	      m_indices(std::move(indices)) { }

	auto toBinary() const;

	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
};

}
