#include "character.hpp"

#include "capsule_collider.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <toast/log.hpp>
#include <toast/physics/physics_settings.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/time.hpp>
#include <toast/window/window_events.hpp>
#include <toast/world/camera.hpp>
#include <tracy/Tracy.hpp>

/**
 * based on Source SDK 2013 gamemovement.cpp
 * https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/refs/heads/master/src/game/shared/gamemovement.cpp
 * 
 * keeps original hl2 bugs lmao
 */

namespace physics {

namespace {

/// Gap kept between the hull and the world
constexpr float k_skin = 0.005f;
constexpr float k_ground_probe = 0.05f;
constexpr float k_non_jump_velocity = 3.556f;		///< Rising faster than this is never standing on ground
constexpr float k_stop_epsilon = 0.00254f;
constexpr float k_min_walk_speed = 0.0254f;
constexpr float k_ground_snap_epsilon = 0.0005f;
constexpr float k_max_pitch = 89.0f;
constexpr int k_max_bumps = 4;
constexpr int k_max_clip_planes = 5;
constexpr int k_depenetration_iterations = 4;

void clipVelocity(const glm::vec3& in, const glm::vec3& normal, glm::vec3& out, float overbounce) {
	out = in - normal * (glm::dot(in, normal) * overbounce);
	for (int axis = 0; axis < 3; ++axis) {
		if (std::abs(out[axis]) < k_stop_epsilon) {
			out[axis] = 0.0f;
		}
	}
	
	if (const float adjust = glm::dot(out, normal); adjust < 0.0f) {
		out -= normal * adjust;
	}
}

auto horizontalDistanceSquared(const glm::vec3& a, const glm::vec3& b) -> float {
	const glm::vec2 delta {a.x - b.x, a.y - b.y};
	return glm::dot(delta, delta);
}

auto wrapDegrees(float degrees) -> float {
	return std::remainder(degrees, 360.0f);
}

auto findCapsule(const toast::Node& node) -> toast::Box<CapsuleCollider> {
	for (const toast::Box<toast::Node>& child : node.children()) {
		if (auto capsule = child.as<CapsuleCollider>(); capsule.exists()) {
			return capsule;
		}
	}
	return {};
}

}

void Character::move(const input::Action& action, input::ActionEvent /*event*/) {
	glm::vec2 value = action.value().as<glm::vec2>();

	const float length = glm::length(value);
	if (action.device() == input::Device::keyboard && length > 1.0e-6f) {
		value /= length;
	}
	setMoveInput(value);
}

void Character::look(const input::Action& action, input::ActionEvent /*event*/) {
	const glm::vec2 value = action.value().as<glm::vec2>();
	if (action.device() == input::Device::mouse) {
		if (m_mouse_locked) {
			addLookInput(glm::vec2 {value.x, -value.y} * mouse_sensitivity);
		}
		return;
	}
	addLookInput(glm::vec2 {value.x, -value.y} * stick_look_speed * static_cast<float>(Time::delta()));
}

void Character::jump(const input::Action& /*action*/, input::ActionEvent event) {
	setJumpHeld(event == input::ActionEvent::start || event == input::ActionEvent::hold);
}

void Character::crouch(const input::Action& /*action*/, input::ActionEvent event) {
	setCrouchHeld(event == input::ActionEvent::start || event == input::ActionEvent::hold);
}

void Character::sprint(const input::Action& /*action*/, input::ActionEvent event) {
	setSprintHeld(event == input::ActionEvent::start || event == input::ActionEvent::hold);
}

void Character::setMoveInput(const glm::vec2& input) {
	const float length = glm::length(input);
	if (not std::isfinite(length)) {
		m_move_input = {};
		return;
	}
	m_move_input = length > 1.0f ? input / length : input;
}

void Character::addLookInput(const glm::vec2& degrees) {
	setViewAngles(view_angles.x + degrees.y, view_angles.y - degrees.x);
}

void Character::setViewAngles(float pitch, float yaw) {
	if (not std::isfinite(pitch) || not std::isfinite(yaw)) {
		return;
	}
	view_angles = {std::clamp(pitch, -k_max_pitch, k_max_pitch), wrapDegrees(yaw)};
}

void Character::setJumpHeld(bool held) {
	if (held && not m_jump_held) {
		m_jump_queued = true;
	}
	m_jump_held = held;
}

void Character::setCrouchHeld(bool held) {
	m_crouch_held = held;
}

void Character::setSprintHeld(bool held) {
	m_sprint_held = held;
}

void Character::teleport(const glm::vec3& position) {
	m_pending_teleport = position;
}

void Character::setVelocity(const glm::vec3& new_velocity) {
	m_pending_velocity = new_velocity;
	m_pending_impulse = {};
}

void Character::addVelocity(const glm::vec3& delta) {
	m_pending_impulse += delta;
}

auto Character::viewDirection() const -> glm::vec3 {
	return yawRotation() * glm::angleAxis(glm::radians(view_angles.x), world_right) * world_forward;
}

auto Character::eyePosition() const -> glm::vec3 {
	return world_position + world_up * m_view_height;
}

void Character::createDefaultChildren() {
	if (findCapsule(*this).exists()) {
		return;
	}

	auto capsule = create("physics::CapsuleCollider").as<CapsuleCollider>();
	if (not capsule.exists()) {
		return;
	}
	capsule->radius = 0.4f;
	capsule->height = 1.83f;
	capsule->position = toast::Node3D::world_up * (capsule->height * 0.5f);
}

void Character::updateInspectorMessages() {
	static const toast::NodeMessage capsule_message {
	  .severity = toast::NodeMessage::error,
	  .id = 23,
	  .text = "Character requires a CapsuleCollider child",
	};
	static const toast::NodeMessage upright_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 24,
	  .text = "Character capsule rotation is ignored, the hull always stays upright",
	};

	const auto capsule = findCapsule(*this);
	if (capsule.exists()) {
		removeInspectorMessage(capsule_message);
	} else {
		addInspectorMessage(capsule_message);
	}

	if (capsule.exists() && std::abs(std::abs(glm::normalize(capsule->rotation).w) - 1.0f) > 1.0e-4f) {
		addInspectorMessage(upright_message);
	} else {
		removeInspectorMessage(upright_message);
	}
}

void Character::onEditorTransformChanged() {
	syncTransform();
	teleport(world_position);
}

void Character::begin() {
	if (not participatesIn(toast::NodeOwnerParticipation::gameplay_tick)) {
		return;
	}

	syncTransform();
	m_origin = world_position;
	m_previous_origin = world_position;
	const glm::vec3 forward = world_rotation * toast::Node3D::world_forward;
	view_angles.y = glm::degrees(std::atan2(-forward.x, forward.y));
	m_view_height = eye_height;

	listener().subscribe<event::WindowMouseLock>("character_mouse_lock", [this](const event::WindowMouseLock& e) {
		m_mouse_locked = e.locked;
		return false;
	});

	if (not view_node.exists()) {
		for (const toast::Box<toast::Node>& child : children()) {
			if (child.as<toast::Camera>().exists()) {
				view_node = child.as<toast::Node3D>();
				break;
			}
		}
	}

	bindHull();
}

void Character::end() {
	listener().unsubscribe<event::WindowMouseLock>("character_mouse_lock");
	m_mouse_locked = false;
	m_bound = false;
	m_capsule = {};
	m_shape = {};
	m_push_hits.clear();
}

void Character::bindHull() {
	m_bound = false;
	m_capsule = findCapsule(*this);
	if (not m_capsule.exists() || Simulator::instance == nullptr || not Simulator::instance->valid(m_capsule->m_shape)) {
		TOAST_WARN("Physics", "Character '{}' has no registered CapsuleCollider child and will not move", name());
		return;
	}

	m_shape = m_capsule->m_shape;
	m_stand_hull = CapsuleShape {.local_center = m_capsule->position, .radius = m_capsule->radius, .height = m_capsule->height};
	m_crouch_hull = m_stand_hull;
	m_crouch_hull.height = std::clamp(crouch_height, 2.0f * m_stand_hull.radius, m_stand_hull.height);
	m_crouch_hull.local_center -= world_up * (0.5f * (m_stand_hull.height - m_crouch_hull.height));

	m_hull = m_stand_hull;
	crouched = false;
	Simulator::instance->setCapsuleShape(m_shape, m_hull);
	m_bound = true;
}

void Character::postPhysics() {
	if (not m_bound) {
		return;
	}

	if (m_jumped_pending) {
		m_jumped_pending = false;
		jumped.fire();
	}
	if (m_landed_pending.has_value()) {
		const float fall_speed = *m_landed_pending;
		m_landed_pending.reset();
		landed.fire(fall_speed);
	}

	world_position = glm::mix(m_previous_origin, m_origin, Simulator::interpolationAlpha());
	world_rotation = yawRotation();
	syncTransform();
	updateView(static_cast<float>(Time::delta()));
}

void Character::updateView(float frame_dt) {
	const float target = crouched ? crouch_eye_height : eye_height;
	const float rate = crouch_view_time > 0.0f ? std::abs(eye_height - crouch_eye_height) / crouch_view_time
	                                           : std::numeric_limits<float>::infinity();
	const float step = rate * frame_dt;
	m_view_height = m_view_height < target ? std::min(m_view_height + step, target) : std::max(m_view_height - step, target);

	if (not view_node.exists()) {
		return;
	}
	view_node->position.z = m_view_height;
	view_node->rotation = glm::angleAxis(glm::radians(view_angles.x), world_right);
	view_node->syncTransform();
}

void Character::simulate(Simulator& simulator, float dt) {
	ZoneScopedN("physics::CharacterMove");
	if (not m_bound || not simulator.valid(m_shape)) {
		return;
	}

	m_simulator = &simulator;
	m_dt = dt;
	m_previous_origin = m_origin;
	m_rotation = yawRotation();

	if (m_pending_teleport.has_value()) {
		m_origin = *m_pending_teleport;
		m_previous_origin = m_origin;
		m_pending_teleport.reset();
		clearGround();
	}
	if (m_pending_velocity.has_value()) {
		velocity = *m_pending_velocity;
		m_pending_velocity.reset();
	}
	velocity += m_pending_impulse;
	m_pending_impulse = {};

	depenetrate();
	duck();
	categorizePosition();

	velocity.z += gravity() * 0.5f * m_dt;
	checkJump();
	if (on_ground) {
		velocity.z = 0.0f;
		applyFriction();
	}
	clampVelocity();

	if (on_ground) {
		walkMove();
	} else {
		airMove();
	}

	categorizePosition();
	clampVelocity();
	if (on_ground) {
		velocity.z = 0.0f;
	} else {
		velocity.z += gravity() * 0.5f * m_dt;
	}

	applyPushes();
	simulator.moveKinematicBody(bodyID(), m_origin, m_rotation, velocity);
	if (m_origin != m_previous_origin) {
		const Shape probe {.type = ShapeType::capsule, .capsule = m_hull};
		const Body body {.type = BodyType::kinematic_body, .position = m_origin, .rotation = m_rotation};
		simulator.wakeBodiesInBounds(worldShapeBounds(body, probe).expanded(k_ground_probe));
	}
	m_simulator = nullptr;
}

auto Character::trace(const glm::vec3& from, const glm::vec3& to) -> SweepHit {
	return m_simulator->sweepCapsule(bodyID(), m_hull, m_rotation, from, to, k_skin);
}

auto Character::fits(const CapsuleShape& hull, const glm::vec3& origin) -> bool {
	return not m_simulator->overlapCapsule(bodyID(), hull, origin, m_rotation, 1.0e-4f, m_contacts);
}

void Character::setHull(const CapsuleShape& hull) {
	m_hull = hull;
	m_simulator->setCapsuleShape(m_shape, hull);
	if (m_capsule.exists()) {
		m_capsule->height = hull.height;
		m_capsule->position = hull.local_center;
	}
}

void Character::depenetrate() {
	for (int iteration = 0; iteration < k_depenetration_iterations; ++iteration) {
		if (not m_simulator->overlapCapsule(bodyID(), m_hull, m_origin, m_rotation, 1.0e-4f, m_contacts)) {
			return;
		}
		const QueryContact& deepest = *std::ranges::max_element(m_contacts, {}, &QueryContact::penetration);
		m_origin += deepest.normal * (deepest.penetration + k_skin * 0.5f);
		if (const float into = glm::dot(velocity, deepest.normal); into < 0.0f) {
			velocity -= deepest.normal * into;
		}
	}
}

void Character::duck() {
	const float shrink = m_stand_hull.height - m_crouch_hull.height;

	if (m_crouch_held && not crouched) {
		// airborne crouch pulls the legs up and keeps the head where it was
		if (not on_ground) {
			m_origin += world_up * shrink;
			m_previous_origin += world_up * shrink;
			m_view_height -= shrink;
		}
		setHull(m_crouch_hull);
		crouched = true;
		return;
	}

	if (not m_crouch_held && crouched) {
		glm::vec3 candidate = on_ground ? m_origin : m_origin - world_up * shrink;
		if (not on_ground && not fits(m_stand_hull, candidate)) {
			candidate = m_origin;
		}
		if (not fits(m_stand_hull, candidate)) {
			return;
		}

		const glm::vec3 delta = candidate - m_origin;
		m_origin = candidate;
		m_previous_origin += delta;
		m_view_height -= glm::dot(delta, world_up);
		setHull(m_stand_hull);
		crouched = false;
	}
}

void Character::categorizePosition() {
	if (velocity.z > k_non_jump_velocity) {
		clearGround();
		return;
	}

	const SweepHit hit = trace(m_origin, m_origin - world_up * k_ground_probe);
	if (hit.hit && hit.normal.z >= walkableNormal()) {
		setGround(hit);
	} else {
		clearGround();
	}
}

void Character::setGround(const SweepHit& hit) {
	if (not on_ground && velocity.z < 0.0f) {
		m_landed_pending = std::max(m_landed_pending.value_or(0.0f), -velocity.z);
	}
	on_ground = true;
	m_ground_body = hit.body;
	m_ground_normal = hit.normal;
}

void Character::clearGround() {
	on_ground = false;
	m_ground_body = {};
	m_ground_normal = toast::Node3D::world_up;
}

void Character::checkJump() {
	const bool wants_jump = m_jump_queued || (auto_bunny_hop && m_jump_held);
	m_jump_queued = false;
	if (not wants_jump || not on_ground) {
		return;
	}

	velocity.z = std::sqrt(2.0f * std::abs(gravity()) * std::max(jump_height, 0.0f));
	applyJumpBoost();
	clearGround();
	m_jumped_pending = true;
}

void Character::applyJumpBoost() {
	const float boost = std::max(crouched || m_sprint_held ? jump_boost_slow : jump_boost, 0.0f);
	if (boost <= 0.0f) {
		return;
	}

	// CHL2GameMovement
	const float forward_move = m_move_input.y * currentMaxSpeed() * (crouched ? crouch_speed_scale : 1.0f);
	const glm::vec3 forward = yawRotation() * toast::Node3D::world_forward;
	const glm::vec3 horizontal_velocity {velocity.x, velocity.y, 0.0f};
	const float boosted_limit = currentMaxSpeed() * (1.0f + boost);
	float addition = std::abs(forward_move * boost);

	if (accelerated_back_hop) {
		// caps total speed but corrects along view
		const float new_speed = addition + glm::length(horizontal_velocity);
		if (new_speed > boosted_limit) {
			addition -= new_speed - boosted_limit;
		}
		if (forward_move < 0.0f) {
			addition = -addition;
		}
		velocity += forward * addition;
		return;
	}

	const glm::vec3 direction = forward_move < 0.0f ? -forward : forward;
	velocity += direction * std::clamp(boosted_limit - glm::dot(horizontal_velocity, direction), 0.0f, addition);
}

void Character::applyFriction() {
	const float speed = glm::length(velocity);
	if (speed < k_stop_epsilon) {
		return;
	}

	const float control = std::max(speed, stop_speed);
	const float new_speed = std::max(speed - control * friction * m_dt, 0.0f);
	velocity *= new_speed / speed;
}

void Character::accelerate(const glm::vec3& wish_direction, float wish_speed, float accel) {
	const float add_speed = wish_speed - glm::dot(velocity, wish_direction);
	if (add_speed <= 0.0f) {
		return;
	}
	velocity += wish_direction * std::min(accel * m_dt * wish_speed, add_speed);
}

void Character::airAccelerate(const glm::vec3& wish_direction, float wish_speed, float accel) {
	// the cap limits the target speed but not the acceleration which still scales with the full wish speed
	const float add_speed = std::min(wish_speed, air_speed_cap) - glm::dot(velocity, wish_direction);
	if (add_speed <= 0.0f) {
		return;
	}
	velocity += wish_direction * std::min(accel * wish_speed * m_dt, add_speed);
}

auto Character::wishVelocity() const -> glm::vec3 {
	const glm::quat yaw = yawRotation();
	glm::vec3 wish = (yaw * toast::Node3D::world_forward * m_move_input.y + yaw * world_right * m_move_input.x) * currentMaxSpeed();
	wish.z = 0.0f;
	return wish;
}

void Character::walkMove() {
	const glm::vec3 wish = wishVelocity();
	const float wish_length = glm::length(wish);
	const glm::vec3 wish_direction = wish_length > 1.0e-6f ? wish / wish_length : glm::vec3 {0.0f};
	const float speed_limit = crouched ? max_speed * crouch_speed_scale : currentMaxSpeed();

	velocity.z = 0.0f;
	accelerate(wish_direction, std::min(wish_length, speed_limit), acceleration);
	velocity.z = 0.0f;

	if (glm::length(velocity) < k_min_walk_speed) {
		velocity = {};
		return;
	}

	const glm::vec3 destination = m_origin + velocity * m_dt;
	if (const SweepHit hit = trace(m_origin, destination); not hit.hit) {
		m_origin = destination;
		stayOnGround();
		return;
	}

	stepMove();
	stayOnGround();
}

void Character::airMove() {
	const glm::vec3 wish = wishVelocity();
	const float wish_length = glm::length(wish);
	const glm::vec3 wish_direction = wish_length > 1.0e-6f ? wish / wish_length : glm::vec3 {0.0f};

	airAccelerate(wish_direction, std::min(wish_length, currentMaxSpeed()), air_acceleration);
	tryPlayerMove();
}

void Character::stepMove() {
	const glm::vec3 start_origin = m_origin;
	const glm::vec3 start_velocity = velocity;

	tryPlayerMove();
	const glm::vec3 down_origin = m_origin;
	const glm::vec3 down_velocity = velocity;

	// same move again lifted by a step and then dropped back onto whatever is below
	m_origin = trace(start_origin, start_origin + toast::Node3D::world_up * step_height).position;
	velocity = start_velocity;
	tryPlayerMove();

	const SweepHit down = trace(m_origin, m_origin - toast::Node3D::world_up * step_height);
	if (not down.hit || down.normal.z < walkableNormal()) {
		m_origin = down_origin;
		velocity = down_velocity;
		return;
	}
	m_origin = down.position;

	if (horizontalDistanceSquared(down_origin, start_origin) > horizontalDistanceSquared(m_origin, start_origin)) {
		m_origin = down_origin;
		velocity = down_velocity;
	} else {
		velocity.z = down_velocity.z;
	}
}

void Character::stayOnGround() {
	const glm::vec3 raised = trace(m_origin, m_origin + toast::Node3D::world_up * k_ground_probe).position;
	const SweepHit hit = trace(raised, m_origin - toast::Node3D::world_up * step_height);
	if (hit.hit && hit.fraction > 0.0f && hit.normal.z >= walkableNormal() &&
	    std::abs(hit.position.z - m_origin.z) > k_ground_snap_epsilon) {
		m_origin = hit.position;
	}
}

void Character::tryPlayerMove() {
	const glm::vec3 primal_velocity = velocity;
	glm::vec3 original_velocity = velocity;
	std::array<glm::vec3, k_max_clip_planes> planes {};
	int plane_count = 0;
	float time_left = m_dt;
	float all_fraction = 0.0f;

	for (int bump = 0; bump < k_max_bumps; ++bump) {
		if (glm::dot(velocity, velocity) == 0.0f) {
			break;
		}

		const SweepHit hit = trace(m_origin, m_origin + velocity * time_left);
		all_fraction += hit.fraction;
		if (hit.fraction > 0.0f) {
			m_origin = hit.position;
			original_velocity = velocity;
			plane_count = 0;
		}
		if (not hit.hit) {
			break;
		}

		if (const float into = -glm::dot(velocity, hit.normal); push_force > 0.0f && into > 0.0f) {
			if (const auto state = m_simulator->state(hit.body); state.has_value() && state->type == BodyType::dynamic_body) {
				m_push_hits.push_back({.body = hit.body, .point = hit.point, .direction = -hit.normal, .speed = into});
			}
		}

		time_left -= time_left * hit.fraction;
		if (plane_count >= k_max_clip_planes) {
			velocity = {};
			break;
		}
		planes[plane_count++] = hit.normal;

		// overbounce
		if (plane_count == 1 && not on_ground) {
			clipVelocity(original_velocity, planes[0], velocity, 1.0f);
			original_velocity = velocity;
			continue;
		}

		int plane = 0;
		for (; plane < plane_count; ++plane) {
			clipVelocity(original_velocity, planes[plane], velocity, 1.0f);
			int other = 0;
			for (; other < plane_count; ++other) {
				if (other != plane && glm::dot(velocity, planes[other]) < 0.0f) {
					break;
				}
			}
			if (other == plane_count) {
				break;
			}
		}

		// slide along the crease of two or stop in a corner
		if (plane == plane_count) {
			const glm::vec3 crease = plane_count == 2 ? glm::cross(planes[0], planes[1]) : glm::vec3 {0.0f};
			const float crease_length = glm::length(crease);
			if (crease_length < 1.0e-6f) {
				velocity = {};
				break;
			}
			const glm::vec3 direction = crease / crease_length;
			velocity = direction * glm::dot(direction, velocity);
		}

		// never bounce back against the original vel
		if (glm::dot(velocity, primal_velocity) <= 0.0f) {
			velocity = {};
			break;
		}
	}

	if (all_fraction == 0.0f) {
		velocity = {};
	}
}

void Character::clampVelocity() {
	for (int axis = 0; axis < 3; ++axis) {
		if (not std::isfinite(velocity[axis])) {
			velocity[axis] = 0.0f;
		}
		velocity[axis] = std::clamp(velocity[axis], -max_velocity, max_velocity);
	}
}

void Character::applyPushes() {
	std::ranges::sort(m_push_hits, [](const PushHit& lhs, const PushHit& rhs) {
		return lhs.body != rhs.body ? lhs.body < rhs.body : lhs.speed > rhs.speed;
	});
	const auto duplicates = std::ranges::unique(m_push_hits, {}, &PushHit::body);
	m_push_hits.erase(duplicates.begin(), duplicates.end());

	for (const PushHit& push : m_push_hits) {
		m_simulator->pushBody(push.body, push.point, push.direction, push.speed, push_force * m_dt);
	}
	m_push_hits.clear();
}

auto Character::currentMaxSpeed() const -> float {
	return m_sprint_held && not crouched ? sprint_speed : max_speed;
}

auto Character::walkableNormal() const -> float {
	return std::cos(glm::radians(std::clamp(max_slope, 0.0f, 89.9f)));
}

auto Character::gravity() const -> float {
	return glm::dot(tunables().gravity, toast::Node3D::world_up) * gravity_scale;
}

auto Character::yawRotation() const -> glm::quat {
	return glm::angleAxis(glm::radians(view_angles.y), toast::Node3D::world_up);
}

}
