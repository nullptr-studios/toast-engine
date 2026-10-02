/// @file DebugDraw.hpp
/// @author dario
/// @date 10/2/2026.

#pragma once

#include "toast/assets/core_types.hpp"

#include <glm/glm.hpp>

namespace toast {
class Camera;
}

namespace assets {
class Texture;
class Mesh;
}

namespace debug {

void TOAST_API drawSolidSphere(glm::vec3 center, float radius, glm::vec4 color);
void TOAST_API drawShapeBox(const glm::mat4& transform, glm::vec4 color, bool fill);
void TOAST_API drawCapsule(const glm::mat4& transform, float radius, float height, glm::vec4 color, bool fill);

void TOAST_API drawLine(glm::vec3 a, glm::vec3 b, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});
void TOAST_API drawBox(glm::vec3 min, glm::vec3 max, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});
void TOAST_API drawSphere(glm::vec3 center, float radius, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, int segments = 24);
void TOAST_API drawAxes(const glm::mat4& transform);

void TOAST_API drawBillboard(
    glm::vec3 world_position, float size, assets::Handle<assets::Texture> texture, glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f}
);

void TOAST_API
    drawMesh(const assets::Handle<assets::Mesh>& mesh, const glm::mat4& transform, glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f});

void TOAST_API drawMesh(toast::UID mesh, const glm::mat4& transform, glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f});

void TOAST_API
    drawBillboard(glm::vec3 world_position, float size, toast::UID texture, glm::vec4 tint = glm::vec4 {1.0f, 1.0f, 1.0f, 1.0f});

void TOAST_API drawArrow(glm::vec3 from, glm::vec3 to, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, float head_size = 0.2f);

void TOAST_API drawCone(
    glm::vec3 apex, glm::vec3 direction, float length, float half_angle_degrees, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
    int segments = 24
);

void TOAST_API
    drawFrustum(const toast::Camera& camera, float aspect, glm::vec4 color = {1.0f, 1.0f, 0.0f, 1.0f}, float far_override = 0.0f);

void TOAST_API drawFrustumFromMatrix(const glm::mat4& view_projection, glm::vec4 color = {1.0f, 1.0f, 0.0f, 1.0f});
}
