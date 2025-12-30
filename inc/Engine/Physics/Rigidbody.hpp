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
	void Inspector() override;
	void EditorTick() override;

	json_t Save() const override;
	void Load(json_t j, bool propagate) override;

	auto GetPosition() const -> glm::dvec2;
	void SetPosition(glm::dvec2);

	void AddForce(glm::dvec2);

	// properties
	double radius = 1.0;
	double mass = 1.0;
	double friction = 0.2;

	// simulation
	glm::vec2 gravityScale { 1.0, 1.0 };
	double linearDrag = 0.5;
	double restitution = 0.6;
	double restitutionThreshold = 0.5;

	// internal
	glm::dvec2 velocity = { 0.0, 0.0 };
	std::deque<glm::dvec2> forces;

private:
	// debug stuff
	struct {
		bool show = true;
		glm::vec2 addForce = { 0.0f, 0.0f };
		glm::vec4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		glm::vec4 collidingColor = { 0.0f, 1.0f, 0.0f, 1.0f };
	} debug;
};

}
