#include "vulkan_renderer.hpp"

#include <cmath>
#include <toast/world/node_3d.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

void VulkanRenderer::registerDebugDraw(toast::Node3D* node, void (*draw)(toast::Node3D&)) {
	std::scoped_lock lock(m_mesh_proxy_mutex);
	if (std::ranges::none_of(m_debug_nodes, [node](const auto& entry) { return entry.first == node; })) {
		m_debug_nodes.emplace_back(node, draw);
	}
}

void VulkanRenderer::unregisterDebugDraw(toast::Node3D* node) {
	std::scoped_lock lock(m_mesh_proxy_mutex);
	std::erase_if(m_debug_nodes, [node](const auto& entry) { return entry.first == node; });
}

namespace {
void triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec4 color) {
	auto& vertices = VulkanRenderer::instance->beginFrameBuild().debug_triangle_vertices;
	vertices.insert(
	    vertices.end(),
	    {
	      {a, color},
        {b, color},
        {c, color}
  }
	);
}

void roundedShape(const glm::mat4& transform, float radius, float shaft, glm::vec4 color, bool fill, bool wire) {
	constexpr int segments = 24;
	constexpr int rings = 17;
	auto point = [&](int ring, int segment) -> glm::vec3 {
		const float latitude =
		    ring <= 8 ? -glm::half_pi<float>() + glm::half_pi<float>() * ring / 8 : glm::half_pi<float>() * (ring - 9) / 8;
		const float longitude = glm::two_pi<float>() * segment / segments;
		const float offset = ring <= 8 ? -shaft : shaft;
		return transform * glm::vec4(
		                       radius * std::cos(latitude) * std::cos(longitude),
		                       radius * std::cos(latitude) * std::sin(longitude),
		                       radius * std::sin(latitude) + offset,
		                       1.0f
		                   );
	};
	for (int i = 0; i < rings; ++i) {
		for (int j = 0; j < segments; ++j) {
			const auto a = point(i, j), b = point(i, j + 1), c = point(i + 1, j + 1), d = point(i + 1, j);
			if (fill && !(shaft == 0.0f && i == 8)) {
				triangle(a, b, c, color);
				triangle(a, c, d, color);
			}
			if (wire) {
				if (j % 6 == 0) {
					debugDrawLine(a, d, color);
				}
				if (i == 8 || i == 9) {
					debugDrawLine(a, b, color);
				}
			}
		}
	}
}
}

void debugDrawSolidSphere(glm::vec3 center, float radius, glm::vec4 color) {
	ZoneScoped;
	if (!std::isfinite(radius) || radius <= 0.0f) {
		return;
	}
	roundedShape(glm::translate(glm::mat4(1.0f), center), radius, 0.0f, color, true, false);
}

void debugDrawCapsule(const glm::mat4& transform, float radius, float height, glm::vec4 color, bool fill) {
	ZoneScoped;
	if (!std::isfinite(radius) || !std::isfinite(height) || radius <= 0.0f || height < 2.0f * radius) {
		return;
	}
	const float shaft = height * 0.5f - radius;
	if (fill) {
		auto fill_color = color;
		fill_color.a *= 0.2f;
		roundedShape(transform, radius, shaft, fill_color, true, false);
	}
	roundedShape(transform, radius, shaft, color, false, true);
}

void debugDrawShapeBox(const glm::mat4& transform, glm::vec4 color, bool fill) {
	ZoneScoped;
	std::array<glm::vec3, 8> corners;
	for (int i = 0; i < 8; ++i) {
		corners[i] = transform * glm::vec4((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f);
	}
	if (fill) {
		auto fill_color = color;
		fill_color.a *= 0.2f;
		constexpr int faces[6][4] = {
		  {0, 2, 3, 1},
      {4, 5, 7, 6},
      {0, 1, 5, 4},
      {2, 6, 7, 3},
      {0, 4, 6, 2},
      {1, 3, 7, 5}
		};
		for (const auto& f : faces) {
			triangle(corners[f[0]], corners[f[1]], corners[f[2]], fill_color);
			triangle(corners[f[0]], corners[f[2]], corners[f[3]], fill_color);
		}
	}
	for (int i = 0; i < 8; ++i) {
		for (int bit : {1, 2, 4}) {
			if (!(i & bit)) {
				debugDrawLine(corners[i], corners[i | bit], color);
			}
		}
	}
}
}
