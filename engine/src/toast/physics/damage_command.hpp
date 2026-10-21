/**
 * @file damage_command.hpp
 * @author Xein
 * @date 18 Sep 2026
 * @brief The kinds of damage that break voxels
 */

#pragma once
#include "body.hpp"
#include "hardness.hpp"
#include "physics_settings.hpp"
#include "shape.hpp"

#include <glm/glm.hpp>
#include <optional>

namespace physics {

struct FragmentPush {
	std::optional<glm::vec3> point;
	glm::vec3 direction {};
	float speed = 0.0f;
	float max_impulse = 0.0f;
};

struct AreaDamage {
	glm::vec3 point {};
	float fracture_radius = 0.0f;
	float energy_density = 0.0f;
	glm::vec3 direction {};
	Hardness max_hardness = k_hardest;
	std::optional<FragmentPush> push;
};

struct ExplosionDamage {
	glm::vec3 point {};
	float force = 0.0f;
	float falloff = tunables().explosion_falloff;
	Hardness max_hardness = k_hardest;
};

struct DamageCommand {
	ShapeID shape;
	glm::vec3 world_center {};
	float fracture_radius = 0.0f;
	float energy_density = 0.0f;
	glm::vec3 direction {};
	Hardness max_hardness = k_hardest;
	BodyID source;
	uint64_t seed = 0;

	float shell_voxels = tunables().fracture_shell_voxels;
	std::optional<FragmentPush> push;
};

struct CapsuleSmash {
	float energy_density = 0.0f;
	glm::vec3 direction {};
	float force = 0.0f;
	float max_speed = 0.0f;
	Hardness max_hardness = k_hardest;
};

}
