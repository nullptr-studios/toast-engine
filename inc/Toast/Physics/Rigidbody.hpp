/// @file Rigidbody.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once

#include "ColliderFlags.hpp"
#include "Toast/Components/Component.hpp"

#include <glm/glm.hpp>

namespace physics {

class Rigidbody : public toast::Component {
public:
	REGISTER_TYPE(Rigidbody);

	void Init() override;
	void Destroy() override;
	void Inspector() override;
	void EditorTick() override;

	json_t Save() const override;
	void Load(json_t j, bool propagate) override;

	auto GetPosition() const -> glm::dvec2;
	void SetPosition(glm::dvec2);

	void AddForce(glm::dvec2);

	// properties
	double radius = 1.0;      // This is not affected by parent.scale()
	double mass = 1.0;        // Weight in kg
	double friction = 0.2;    // How it deals frictions onto other objects (not itself)
	ColliderFlags flags = ColliderFlags::Default;

	// simulation
	glm::vec2 gravityScale { 1.0, 1.0 };          // How much gravity affects the object
	glm::vec2 drag = {0.5, 0.5};                  // Anisotropic drag for linear movement
	double restitution = 0.6;                     // Bounciness
	double restitutionThreshold = 0.5;            // Minimum speed for restitution to take place
	glm::vec2 minimumVelocity { 0.1, 0.1 };    // Object velocity will be set to 0.0 if less than this

	// internal
	glm::dvec2 velocity = { 0.0, 0.0 };
	std::deque<glm::dvec2> forces;

	// debug stuff
	struct {
		bool show = true;                                         // Draws the debug of the shape
		bool showManifolds = false;                               // Shows contact points and normal resolutions
		glm::vec2 addForce = { 0.0f, 0.0f };
		glm::vec4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };      // Color when not colliding
		glm::vec4 collidingColor = { 0.0f, 1.0f, 0.0f, 1.0f };    // Color when colliding (not implemented)
	} debug;
};


}
