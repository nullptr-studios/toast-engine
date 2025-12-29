/// @file Rigidbody.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once

#include <Engine/Toast/Components/Component.hpp>
#include <glm/glm.hpp>

namespace physics {

class Rigidbody : public toast::Component {
public:
	REGISTER_ABSTRACT(Rigidbody);

	void Init() override;
	void Destroy() override;

	double radius = 1.0;
	double mass = 1.0;

	double friction = 0.2;
	double linearDrag = 0.2;
	double restitution = 0.6;
	double restitutionThreshold = 0.5;

	glm::dvec2 velocity = { 1.0, 1.0 };
	glm::dvec2 gravityScale { 1.0, 1.0 };
	std::deque<glm::dvec2> forces;

	glm::dvec2 GetPosition();
	void SetPosition(glm::dvec2);
};

}
