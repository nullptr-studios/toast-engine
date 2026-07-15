/// @file vertex.hpp
/// @author dario
/// @date 07/06/2026.

#pragma once

#include <glm/glm.hpp>

namespace toast::renderer {

/// @brief Vertex with position, normals, UVs, tangents, and colors for mesh rendering
struct Vertex {
	glm::vec<3, float, glm::packed_highp> position;
	glm::vec<3, float, glm::packed_highp> normal;
	glm::vec<2, float, glm::packed_highp> uv;
	glm::vec<4, float, glm::packed_highp> tangent;
	glm::vec<3, float, glm::packed_highp> color;

	/*
	JOINTS_0
	WEIGHTS_0
	*/
};

}
