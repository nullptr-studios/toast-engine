#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <toast/assets/mesh.hpp>

namespace {

auto nearly(float a, float b) -> bool {
	return std::fabs(a - b) < 1e-4f;
}

auto makeVertex(glm::vec3 position, glm::vec3 normal, glm::vec2 uv) -> renderer::Vertex {
	renderer::Vertex v {};
	v.position = position;
	v.normal = normal;
	v.uv = uv;
	return v;
}

}

// A glTF only has to ship TANGENT when a material samples a normal map, so meshes routinely arrive without
// one. Left at zero, mesh.slang's TBN reconstruction produces a NaN normal and every light contributes
// nothing - the surface renders lit by the ambient term alone
TOAST_TEST_NAMED("Assets", "assets/05-tangent-generation", test_assets_05_tangent_generation) {
	// Quad in the XY plane, normal +Z, U running along +X and V along +Y
	std::vector<renderer::Vertex> vertices {
	  makeVertex({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}),
	  makeVertex({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}),
	  makeVertex({1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}),
	  makeVertex({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}),
	};
	const std::vector<uint32_t> indices {0, 1, 2, 0, 2, 3};

	assets::generateTangents(vertices, indices);

	for (const auto& v : vertices) {
		const glm::vec3 tangent {v.tangent};
		const glm::vec3 normal {v.normal};

		// Unit length and perpendicular to the normal, or the per-pixel Gram-Schmidt in mesh.slang has
		// nothing valid to build a bitangent from
		assert(nearly(glm::length(tangent), 1.0f));
		assert(nearly(glm::dot(tangent, normal), 0.0f));

		// U runs along +X here, so that is where the tangent has to point
		assert(nearly(tangent.x, 1.0f));

		// Handedness is never zero: mesh.slang multiplies the bitangent by it, and a zero would collapse
		// the frame exactly like a missing tangent does
		assert(nearly(std::fabs(v.tangent.w), 1.0f));
		// glTF's convention is bitangent = cross(N, T) * w. Here cross(+Z, +X) is +Y, which is already the
		// direction V runs in, so the frame is right-handed
		assert(nearly(v.tangent.w, 1.0f));

		const glm::vec3 bitangent = glm::cross(normal, tangent) * v.tangent.w;
		assert(nearly(bitangent.y, 1.0f));
	}

	// A vertex no usable triangle reaches still has to come out with a valid frame rather than zeros - it
	// is the zero tangent itself that breaks shading, not the lack of a meaningful direction
	std::vector<renderer::Vertex> degenerate {
	  makeVertex({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}),
	  makeVertex({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}),
	  makeVertex({1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}),
	};
	const std::vector<uint32_t> degenerate_indices {0, 1, 2};

	assets::generateTangents(degenerate, degenerate_indices);

	for (const auto& v : degenerate) {
		const glm::vec3 tangent {v.tangent};
		assert(nearly(glm::length(tangent), 1.0f));
		assert(nearly(glm::dot(tangent, glm::vec3(v.normal)), 0.0f));
		assert(nearly(std::fabs(v.tangent.w), 1.0f));
	}

	// Out-of-range indices must be skipped rather than writing past the vertex array
	std::vector<renderer::Vertex> single {makeVertex({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f})};
	assets::generateTangents(single, {0, 7, 9});
	assert(nearly(glm::length(glm::vec3(single[0].tangent)), 1.0f));
}
