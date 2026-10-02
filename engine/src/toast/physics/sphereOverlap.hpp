/**
 * @file sphereOverlap.hpp
 * @author Dante Harper
 * @date 02 Oct 26
 */

#pragma once

#include "toast/world/node.hpp"

#include <glm/glm.hpp>
#include <optional>
#include <toast/export.hpp>

namespace physics {

struct SphereHit {
	toast::Box<toast::Node> node;
	glm::vec3 position;
	glm::vec3 normal;
	float penetration;
};

TOAST_API auto sphereOverlap(glm::vec3 position, float radius) -> std::optional<SphereHit>;
}
