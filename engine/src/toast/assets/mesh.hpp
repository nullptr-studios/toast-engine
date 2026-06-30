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
#include <toast/export.hpp>
#include <toast/log.hpp>

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
	using Index = uint32_t;

	struct Vertex {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec3 tangent;
		glm::vec3 color;
	};

	explicit Mesh(const std::vector<uint8_t>& data);

	Mesh(std::string_view name, std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices)
	    : m_name(name),
	      m_vertices(std::move(vertices)),
	      m_indices(std::move(indices)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "mesh";
	}

	[[nodiscard]]
	auto vertices() const -> const std::vector<Vertex>& {
		return m_vertices;
	}

	[[nodiscard]]
	auto indices() const -> const std::vector<Index>& {
		return m_indices;
	}

	[[nodiscard]]
	auto name() const -> const std::string& {
		return m_name;
	}

	[[nodiscard]]
	auto toBinary() const -> std::vector<uint8_t>;

private:
	std::string m_name;
	std::vector<Vertex> m_vertices;
	std::vector<Index> m_indices;
};

}
