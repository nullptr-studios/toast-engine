/**
 * @file debug_visuals.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief How the simulation is drawn for the debug parameters
 */

#pragma once
#include <glm/glm.hpp>
#include <toast/export.hpp>
#include <toast/physics/body.hpp>

namespace physics {

[[nodiscard]]
TOAST_API auto debugColor(const Body& body) -> glm::vec4;

}
