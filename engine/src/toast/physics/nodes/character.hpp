/**
 * @file character.hpp
 * @author Dario
 * @date 23 Sep 2026
 */

#pragma once
#include "../shape_query.hpp"
#include "kinematic_rigidbody.hpp"

#include <optional>
#include <toast/input/action.hpp>
#include <vector>

namespace physics {

class CapsuleCollider;

/**
 * @brief Source engine movement
 * runs inside fixed physics step
 */
class [[ToastNode, Icon("CharacterBody")]] TOAST_API Character : public physics::KinematicRigidbody {
	friend class Simulator;

public:
	signals::Signal<> jumped;
	signals::Signal<float> landed;    ///< Fall speed at the moment of touching ground

	[[Reflect]]
	void move(const input::Action& action, input::ActionEvent event);
	[[Reflect]]
	void look(const input::Action& action, input::ActionEvent event);
	[[Reflect]]
	void jump(const input::Action& action, input::ActionEvent event);
	[[Reflect]]
	void crouch(const input::Action& action, input::ActionEvent event);
	[[Reflect]]
	void sprint(const input::Action& action, input::ActionEvent event);

	[[Reflect]]
	void setMoveInput(const glm::vec2& input);
	/// TODO: Improve camera feeling
	[[Reflect]]
	void addLookInput(const glm::vec2& degrees);
	[[Reflect]]
	void setViewAngles(float pitch, float yaw);
	[[Reflect]]
	void setJumpHeld(bool held);
	[[Reflect]]
	void setCrouchHeld(bool held);
	[[Reflect]]
	void setSprintHeld(bool held);

	[[Reflect]]
	void teleport(const glm::vec3& position);
	[[Reflect]]
	void setVelocity(const glm::vec3& new_velocity);
	[[Reflect]]
	void addVelocity(const glm::vec3& delta);

	[[Reflect]]
	auto viewDirection() const -> glm::vec3;
	[[Reflect]]
	auto eyePosition() const -> glm::vec3;

	/// Called by the editor right after creating the node
	/// HACK: PLEASE FIX
	[[Reflect]]
	void createDefaultChildren();

	[[Reflect, Group("Ground"), Unit("m/s")]]
	float max_speed = 6.35f;
	[[Reflect, Group("Ground"), Unit("m/s")]]
	float sprint_speed = 8.13f;
	[[Reflect, Group("Ground")]]
	float acceleration = 10.0f;
	[[Reflect, Group("Ground")]]
	float friction = 4.0f;
	[[Reflect, Group("Ground"), Unit("m/s")]]
	float stop_speed = 2.54f;
	[[Reflect, Group("Ground"), Unit("°"), Range(0, 89)]]
	float max_slope = 45.57f;
	[[Reflect, Group("Ground"), Unit("m")]]
	float step_height = 0.457f;

	[[Reflect, Group("Air")]]
	float air_acceleration = 10.0f;
	[[Reflect, Group("Air"), Unit("m/s")]]
	float air_speed_cap = 0.762f;    ///< Strafe jumping needs this below max speed
	[[Reflect, Group("Air")]]
	float gravity_scale = 2.07f;
	[[Reflect, Group("Air"), Unit("m/s")]]
	float max_velocity = 88.9f;

	[[Reflect, Group("Jump"), Unit("m")]]
	float jump_height = 1.14f;
	[[Reflect, Group("Jump")]]
	bool auto_bunny_hop = false;
	[[Reflect, Group("Jump"), Range(0, 1)]]
	float jump_boost = 0.5f;
	[[Reflect, Group("Jump"), Range(0, 1)]]
	float jump_boost_slow = 0.1f;
	[[Reflect, Group("Jump")]]
	bool accelerated_back_hop = true;

	[[Reflect, Group("Crouch"), Unit("m")]]
	float crouch_height = 0.914f;
	[[Reflect, Group("Crouch")]]
	float crouch_speed_scale = 0.34f;

	/// Positioned at eye height and pitched
	[[Reflect, Group("View")]]
	toast::Box<toast::Node3D> view_node;
	[[Reflect, Group("View"), Unit("m")]]
	float eye_height = 1.63f;
	[[Reflect, Group("View"), Unit("m")]]
	float crouch_eye_height = 0.71f;
	[[Reflect, Group("View"), Unit("s")]]
	float crouch_view_time = 0.2f;
	[[Reflect, Group("View"), Unit("°/px")]]
	float mouse_sensitivity = 0.066f;
	[[Reflect, Group("View"), Unit("°/s")]]
	float stick_look_speed = 180.0f;

	/// Cap on how hard the character shoves dynamic bodies it walks into
	[[Reflect, Group("Pushing"), Unit("N")]]
	float push_force = 400.0f;

	[[Reflect, ReadOnly, Group("State"), Unit("m/s")]]
	glm::vec3 velocity = {};
	[[Reflect, ReadOnly, Group("State")]]
	bool on_ground = false;
	[[Reflect, ReadOnly, Group("State")]]
	bool crouched = false;
	[[Reflect, ReadOnly, Group("State"), Unit("°")]]
	glm::vec2 view_angles = {};    ///< x pitch y yaw

private:
	struct PushHit {
		BodyID body;
		glm::vec3 point;
		glm::vec3 direction;
		float speed;
	};

	void updateInspectorMessages() override;
	void onEditorTransformChanged() override;
	void begin();
	void end();
	void postPhysics();

	void simulate(Simulator& simulator, float dt);
	void bindHull();
	void setHull(const CapsuleShape& hull);
	void depenetrate();
	void duck();
	void categorizePosition();
	void setGround(const SweepHit& hit);
	void clearGround();
	void checkJump();
	void applyJumpBoost();
	void applyFriction();
	void accelerate(const glm::vec3& wish_direction, float wish_speed, float accel);
	void airAccelerate(const glm::vec3& wish_direction, float wish_speed, float accel);
	void walkMove();
	void airMove();
	void stepMove();
	void stayOnGround();
	void tryPlayerMove();
	void clampVelocity();
	void applyPushes();
	void updateView(float frame_dt);

	[[nodiscard]]
	auto trace(const glm::vec3& from, const glm::vec3& to) -> SweepHit;
	[[nodiscard]]
	auto fits(const CapsuleShape& hull, const glm::vec3& origin) -> bool;
	[[nodiscard]]
	auto wishVelocity() const -> glm::vec3;
	[[nodiscard]]
	auto walkableNormal() const -> float;
	[[nodiscard]]
	auto currentMaxSpeed() const -> float;
	[[nodiscard]]
	auto gravity() const -> float;
	[[nodiscard]]
	auto yawRotation() const -> glm::quat;

	Simulator* m_simulator = nullptr;
	float m_dt = 0.0f;

	toast::Box<CapsuleCollider> m_capsule;
	ShapeID m_shape;
	CapsuleShape m_stand_hull;
	CapsuleShape m_crouch_hull;
	CapsuleShape m_hull;
	glm::quat m_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

	glm::vec3 m_origin = {};
	glm::vec3 m_previous_origin = {};
	glm::vec3 m_ground_normal = {0.0f, 0.0f, 1.0f};
	BodyID m_ground_body;
	std::vector<PushHit> m_push_hits;
	std::vector<QueryContact> m_contacts;

	glm::vec2 m_move_input = {};
	bool m_mouse_locked = false;
	bool m_jump_held = false;
	bool m_jump_queued = false;
	bool m_crouch_held = false;
	bool m_sprint_held = false;

	std::optional<glm::vec3> m_pending_teleport;
	std::optional<glm::vec3> m_pending_velocity;
	glm::vec3 m_pending_impulse = {};

	float m_view_height = 0.0f;
	bool m_jumped_pending = false;
	std::optional<float> m_landed_pending;
	bool m_bound = false;
};

}
