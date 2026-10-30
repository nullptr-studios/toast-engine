/// @file vertex.hpp
/// @author dario
/// @date 07/06/2026

#pragma once

#include <array>
#include <cstddef>
#include <glm/glm.hpp>

namespace renderer {

struct Vertex {
	glm::vec<3, float> position;
	glm::vec<3, float> normal;
	glm::vec<2, float> uv;
	glm::vec<4, float> tangent;

	glm::vec<3, float> color {1.0f, 1.0f, 1.0f};
};

struct SkinVertex {
	glm::vec<4, uint16_t> joints {0, 0, 0, 0};
	glm::vec<4, float> weights {0.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(Vertex) == 60, "skinning.slang's kVertexStride");
static_assert(offsetof(Vertex, position) == 0, "skinning.slang's kVertexPositionOffset");
static_assert(offsetof(Vertex, normal) == 12, "skinning.slang's kVertexNormalOffset");
static_assert(offsetof(Vertex, uv) == 24, "skinning.slang's kVertexUvOffset");
static_assert(offsetof(Vertex, tangent) == 32, "skinning.slang's kVertexTangentOffset");
static_assert(offsetof(Vertex, color) == 48, "skinning.slang's kVertexColorOffset");

static_assert(sizeof(SkinVertex) == 24, "skinning.slang's kSkinStride");
static_assert(offsetof(SkinVertex, joints) == 0, "skinning.slang's kSkinJointsOffset");
static_assert(offsetof(SkinVertex, weights) == 8, "skinning.slang's kSkinWeightsOffset");

}
