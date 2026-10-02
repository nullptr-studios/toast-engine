/// @file DebugDraw.cpp
/// @author dario
/// @date 10/2/2026.

#include "DebugDraw.hpp"

#include "vulkan_renderer.hpp"

#include <toast/assets/assets.hpp>
#include <toast/assets/mesh.hpp>
#include <toast/assets/texture.hpp>
#include <toast/world/camera.hpp>

namespace debug {

namespace {
void triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec4 color) {
	auto& vertices = renderer::VulkanRenderer::instance->getCurrentFrameBuild().debug_triangle_vertices;
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
		    ring <= 8 ? -glm::half_pi<float>() + ((glm::half_pi<float>() * ring) / 8) : (glm::half_pi<float>() * (ring - 9)) / 8;
		const float longitude = glm::two_pi<float>() * segment / segments;
		const float offset = ring <= 8 ? -shaft : shaft;
		return transform * glm::vec4(
		                       radius * std::cos(latitude) * std::cos(longitude),
		                       radius * std::cos(latitude) * std::sin(longitude),
		                       (radius * std::sin(latitude)) + offset,
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
					debug::drawLine(a, d, color);
				}
				if (i == 8 || i == 9) {
					debug::drawLine(a, b, color);
				}
			}
		}
	}
}
}

/*



*/

void drawSolidSphere(glm::vec3 center, float radius, glm::vec4 color) {
	ZoneScoped;
	if (!std::isfinite(radius) || radius <= 0.0f) {
		return;
	}
	roundedShape(glm::translate(glm::mat4(1.0f), center), radius, 0.0f, color, true, false);
}

void drawCapsule(const glm::mat4& transform, float radius, float height, glm::vec4 color, bool fill) {
	ZoneScoped;
	if (!std::isfinite(radius) || !std::isfinite(height) || radius <= 0.0f || height < 2.0f * radius) {
		return;
	}
	const float shaft = (height * 0.5f) - radius;
	if (fill) {
		auto fill_color = color;
		fill_color.a *= 0.2f;
		roundedShape(transform, radius, shaft, fill_color, true, false);
	}
	roundedShape(transform, radius, shaft, color, false, true);
}

void drawShapeBox(const glm::mat4& transform, glm::vec4 color, bool fill) {
	ZoneScoped;
	std::array<glm::vec3, 8> corners;
	for (int i = 0; i < 8; ++i) {
		corners[i] = transform * glm::vec4((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f);
	}
	if (fill) {
		auto fill_color = color;
		fill_color.a *= 0.2f;
		constexpr std::array<std::array<int, 4>, 6> faces = {
		  {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}}
		};
		for (const auto& f : faces) {
			triangle(corners[f[0]], corners[f[1]], corners[f[2]], fill_color);
			triangle(corners[f[0]], corners[f[2]], corners[f[3]], fill_color);
		}
	}
	for (int i = 0; i < 8; ++i) {
		for (int bit : {1, 2, 4}) {
			if (!(i & bit)) {
				debug::drawLine(corners[i], corners[i | bit], color);
			}
		}
	}
}

void drawLine(glm::vec3 a, glm::vec3 b, glm::vec4 color) {
	if (!renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	renderer::VulkanRenderer::instance->queueDebugLine(a, b, color);
}

void drawBox(glm::vec3 min, glm::vec3 max, glm::vec4 color) {
	if (!renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	const std::array<glm::vec3, 8> corners {
	  glm::vec3 {min.x, min.y, min.z},
	  glm::vec3 {max.x, min.y, min.z},
	  glm::vec3 {max.x, max.y, min.z},
	  glm::vec3 {min.x, max.y, min.z},
	  glm::vec3 {min.x, min.y, max.z},
	  glm::vec3 {max.x, min.y, max.z},
	  glm::vec3 {max.x, max.y, max.z},
	  glm::vec3 {min.x, max.y, max.z},
	};
	static constexpr std::array<std::pair<int, int>, 12> edges {
	  {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
	};
	for (const auto& [a, b] : edges) {
		drawLine(corners[a], corners[b], color);
	}
}

void drawSphere(glm::vec3 center, float radius, glm::vec4 color, int segments) {
	if (!renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	for (int axis = 0; axis < 3; ++axis) {
		glm::vec3 prev {};
		for (int i = 0; i <= segments; ++i) {
			const float t = (static_cast<float>(i) / static_cast<float>(segments)) * glm::two_pi<float>();
			glm::vec3 p {};
			switch (axis) {
				case 0: p = center + glm::vec3(0.0f, std::cos(t), std::sin(t)) * radius; break;
				case 1: p = center + glm::vec3(std::cos(t), 0.0f, std::sin(t)) * radius; break;
				default: p = center + glm::vec3(std::cos(t), std::sin(t), 0.0f) * radius; break;
			}
			if (i > 0) {
				drawLine(prev, p, color);
			}
			prev = p;
		}
	}
}

void debugDrawAxes(const glm::mat4& transform) {
	if (!renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	renderer::VulkanRenderer::instance->getCurrentFrameBuild().debug_gizmo_instances.push_back(transform);
}

void drawBillboard(glm::vec3 world_position, float size, assets::Handle<assets::Texture> texture, glm::vec4 tint) {
	if (!texture.hasValue() || !renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	renderer::VulkanRenderer::instance->getCurrentFrameBuild().debug_billboards.push_back(
	    renderer::VulkanRenderer::DebugBillboard {
	      .position = world_position,
	      .size = size,
	      .tint = tint,
	      .texture = std::move(texture),
	    }
	);
}

void drawBillboard(glm::vec3 world_position, float size, toast::UID texture, glm::vec4 tint) {
	if (texture.data() == 0 || !renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	drawBillboard(world_position, size, assets::load<assets::Texture>(texture), tint);
}

void drawMesh(const assets::Handle<assets::Mesh>& mesh, const glm::mat4& transform, glm::vec4 tint) {
	if (!mesh.hasValue() || !renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	renderer::VulkanRenderer::instance->getCurrentFrameBuild().debug_meshes.push_back(
	    renderer::VulkanRenderer::DebugMesh {.model = transform, .tint = tint, .mesh = mesh}
	);
}

void drawMesh(toast::UID mesh, const glm::mat4& transform, glm::vec4 tint) {
	if (!renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	drawMesh(assets::load<assets::Mesh>(mesh), transform, tint);
}

void drawArrow(glm::vec3 from, glm::vec3 to, glm::vec4 color, float head_size) {
	drawLine(from, to, color);

	const glm::vec3 dir = to - from;
	const float len = glm::length(dir);
	if (len < 0.0001f) {
		return;
	}
	const glm::vec3 axis = dir / len;
	const glm::vec3 up = std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
	const glm::vec3 side = glm::normalize(glm::cross(up, axis));

	const glm::vec3 back = to - axis * head_size;
	drawLine(to, back + side * head_size * 0.5f, color);
	drawLine(to, back - side * head_size * 0.5f, color);
}

void drawCone(glm::vec3 apex, glm::vec3 direction, float length, float half_angle_degrees, glm::vec4 color, int segments) {
	if (!renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	const float dir_len = glm::length(direction);
	const glm::vec3 axis = dir_len > 0.0001f ? direction / dir_len : glm::vec3(0.0f, 0.0f, -1.0f);

	const glm::vec3 up = std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
	const glm::vec3 u = glm::normalize(glm::cross(up, axis));
	const glm::vec3 w = glm::cross(axis, u);

	const float radius = length * std::tan(glm::radians(half_angle_degrees));
	const glm::vec3 base_center = apex + axis * length;

	std::vector<glm::vec3> ring(static_cast<size_t>(segments));
	for (int i = 0; i < segments; ++i) {
		const float t = (static_cast<float>(i) / static_cast<float>(segments)) * glm::two_pi<float>();
		ring[static_cast<size_t>(i)] = base_center + (u * std::cos(t) + w * std::sin(t)) * radius;
	}

	for (int i = 0; i < segments; ++i) {
		const int next = (i + 1) % segments;
		drawLine(ring[static_cast<size_t>(i)], ring[static_cast<size_t>(next)], color);
	}

	constexpr int k_spokes = 4;
	for (int i = 0; i < k_spokes; ++i) {
		const int idx = (i * segments) / k_spokes;
		drawLine(apex, ring[static_cast<size_t>(idx)], color);
	}
}

void drawFrustum(const toast::Camera& camera, float aspect, glm::vec4 color, float far_override) {
	if (!renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	const float far_distance = far_override > 0.0f ? std::min(far_override, camera.far_plane) : camera.far_plane;

	const float tan_half_fov_y = std::tan(glm::radians(camera.fov) * 0.5f);
	const float near_height = 2.0f * tan_half_fov_y * camera.near_plane;
	const float near_width = near_height * aspect;
	const float far_height = 2.0f * tan_half_fov_y * far_distance;
	const float far_width = far_height * aspect;

	const std::array<glm::vec3, 8> view_space_corners {
	  glm::vec3 {-near_width * 0.5f, -near_height * 0.5f, -camera.near_plane},
	  glm::vec3 { near_width * 0.5f, -near_height * 0.5f, -camera.near_plane},
	  glm::vec3 { near_width * 0.5f,  near_height * 0.5f, -camera.near_plane},
	  glm::vec3 {-near_width * 0.5f,  near_height * 0.5f, -camera.near_plane},
	  glm::vec3 { -far_width * 0.5f,  -far_height * 0.5f,      -far_distance},
	  glm::vec3 {  far_width * 0.5f,  -far_height * 0.5f,      -far_distance},
	  glm::vec3 {  far_width * 0.5f,   far_height * 0.5f,      -far_distance},
	  glm::vec3 { -far_width * 0.5f,   far_height * 0.5f,      -far_distance},
	};

	const glm::mat4 inv_view = glm::inverse(camera.getView());
	std::array<glm::vec3, 8> world_corners {};
	for (int i = 0; i < 8; ++i) {
		world_corners[i] = glm::vec3(inv_view * glm::vec4(view_space_corners[i], 1.0f));
	}

	static constexpr std::array<std::pair<int, int>, 12> edges {
	  {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
	};
	for (const auto& [a, b] : edges) {
		drawLine(world_corners[a], world_corners[b], color);
	}
}

void drawFrustumFromMatrix(const glm::mat4& view_projection, glm::vec4 color) {
	if (!renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	const glm::mat4 inverse = glm::inverse(view_projection);

	static const std::array<glm::vec3, 8> ndc_corners {
	  glm::vec3 {-1.0f, -1.0f, 0.0f},
	  glm::vec3 { 1.0f, -1.0f, 0.0f},
	  glm::vec3 { 1.0f,  1.0f, 0.0f},
	  glm::vec3 {-1.0f,  1.0f, 0.0f},
	  glm::vec3 {-1.0f, -1.0f, 1.0f},
	  glm::vec3 { 1.0f, -1.0f, 1.0f},
	  glm::vec3 { 1.0f,  1.0f, 1.0f},
	  glm::vec3 {-1.0f,  1.0f, 1.0f},
	};

	std::array<glm::vec3, 8> world_corners {};
	for (int i = 0; i < 8; ++i) {
		const glm::vec4 unprojected = inverse * glm::vec4(ndc_corners[static_cast<size_t>(i)], 1.0f);
		world_corners[static_cast<size_t>(i)] = glm::vec3(unprojected) / unprojected.w;
	}

	static constexpr std::array<std::pair<int, int>, 12> edges {
	  {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
	};
	for (const auto& [a, b] : edges) {
		drawLine(world_corners[static_cast<size_t>(a)], world_corners[static_cast<size_t>(b)], color);
	}
}
}
