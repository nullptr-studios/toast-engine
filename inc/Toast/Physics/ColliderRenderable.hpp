/**
 * @file ColliderRenderable.hpp
 * @author Xein
 * @date 16/03/26
 *
 * @brief [TODO: Brief description of the file's purpose]
 */

#pragma once
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include <Toast/Renderer/IRenderable.hpp>
#include <glm/glm.hpp>

namespace physics {

class ColliderRenderable : public renderer::IRenderable {
	struct {
		std::vector<glm::vec2> points;
		std::vector<size_t> indices;
	} m;

public:
	void SendVertices(std::vector<glm::vec2>& points);

	void OnRender(const glm::mat4& viewProjection) noexcept override {
		for (size_t i = 0; i < m.indices.size(); i += 3) {
			renderer::DebugLine(m.points[m.indices[i]], m.points[m.indices[i+1]], glm::vec4{0.0, 1.0, 1.0, 1.0});
			renderer::DebugLine(m.points[m.indices[i+1]], m.points[m.indices[i+2]], glm::vec4{0.0, 1.0, 1.0, 1.0});
			renderer::DebugLine(m.points[m.indices[i+2]], m.points[m.indices[i]], glm::vec4{0.0, 1.0, 1.0, 1.0});
		}
	}
};

}
