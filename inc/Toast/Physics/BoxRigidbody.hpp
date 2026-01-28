/// @file BoxRigidbody.hpp
/// @author Xein
/// @date 1 Jan 2026

#pragma once
#include "Line.hpp"

#include <Toast/Components/Component.hpp>
#include <glm/glm.hpp>

namespace physics {

class BoxRigidbody : public toast::Component {
public:
	REGISTER_ABSTRACT(BoxRigidbody);

	void Init() override;
	void Destroy() override;
#ifdef TOAST_EDITOR
	void Inspector() override;
#endif
	void EditorTick() override;

	json_t Save() const override;
	void Load(json_t j, bool b) override;

	auto GetPosition() const -> glm::dvec2;
	void SetPosition(glm::dvec2);
	auto GetRotation() const -> double;
	void SetRotation(double);

	void AddForce(glm::dvec2 force);
	void AddForce(glm::dvec2 force, glm::dvec2 position);
	void AddTorque(double torque);

	auto GetPoints() const -> std::vector<glm::vec2>;
	auto GetEdges() const -> std::vector<Line>;

	// properties
	glm::dvec2 size;
	glm::dvec2 offset;
	double rotation;
	double mass = 1.0;
	double friction = 0.2;

	// simulation
	double linearDrag = 0.5;
	double angularDrag = 0.5;
	double restitution = 0.6;
	double restitutionThreshold = 0.5;
	glm::vec2 gravityScale { 1.0, 1.0 };
	glm::dvec2 minimumVelocity { 0.01, 0.01 };
	double minimumAngularVelocity = 0.01;
	bool disableAngular = false;

	// internal
	glm::dvec2 velocity = { 0.0, 0.0 };
	double angularVelocity = 0.0;
	std::deque<glm::dvec2> forces;
	std::deque<double> torques;

	// debug stuff
	struct {
		bool show = true;
		bool showManifolds = false;
		glm::vec2 addForce = { 0.0f, 0.0f };
		glm::vec4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		glm::vec4 collidingColor = { 0.0f, 1.0f, 0.0f, 1.0f };
	} debug;
};

}
