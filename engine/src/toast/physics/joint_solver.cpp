#include "joint_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace physics::_detail {

namespace {

constexpr float k_infinity = std::numeric_limits<float>::infinity();
constexpr float k_max_position_correction = 0.2f;
constexpr float k_min_hinge_arm = 0.01f;

auto skew(const glm::vec3& v) -> glm::mat3 {
	// skew(v) * x = cross(v, x)
	return glm::mat3 {0.0f, v.z, -v.y, -v.z, 0.0f, v.x, v.y, -v.x, 0.0f};
}

auto inverseMass(const Body& body) -> float {
	return movable(body) ? body.inverse_mass : 0.0f;
}

auto inverseInertia(const Body& body) -> glm::mat3 {
	return movable(body) ? body.inverse_inertia_world : glm::mat3 {0.0f};
}

auto velocityAt(const Body& body, const glm::vec3& r) -> glm::vec3 {
	return body.linear_velocity + glm::cross(body.angular_velocity, r);
}

void applyLinear(Body& a, Body& b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& impulse) {
	if (movable(a)) {
		a.linear_velocity -= impulse * a.inverse_mass;
		a.angular_velocity -= a.inverse_inertia_world * glm::cross(r_a, impulse);
	}
	if (movable(b)) {
		b.linear_velocity += impulse * b.inverse_mass;
		b.angular_velocity += b.inverse_inertia_world * glm::cross(r_b, impulse);
	}
}

void applyAngular(Body& a, Body& b, const glm::vec3& impulse) {
	if (movable(a)) {
		a.angular_velocity -= a.inverse_inertia_world * impulse;
	}
	if (movable(b)) {
		b.angular_velocity += b.inverse_inertia_world * impulse;
	}
}

auto linearMass(const Body& a, const Body& b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& direction) -> float {
	const glm::vec3 angular_a = inverseInertia(a) * glm::cross(r_a, direction);
	const glm::vec3 angular_b = inverseInertia(b) * glm::cross(r_b, direction);
	const float denominator =
	    inverseMass(a) + inverseMass(b) + glm::dot(direction, glm::cross(angular_a, r_a) + glm::cross(angular_b, r_b));
	return std::isfinite(denominator) && denominator > 1.0e-8f ? 1.0f / denominator : 0.0f;
}

auto angularMass(const Body& a, const Body& b, const glm::vec3& axis) -> float {
	const float denominator = glm::dot(axis, (inverseInertia(a) + inverseInertia(b)) * axis);
	return std::isfinite(denominator) && denominator > 1.0e-8f ? 1.0f / denominator : 0.0f;
}

auto limitBias(float error, float inverse_dt) -> float {
	return error > 0.0f ? error * inverse_dt : 0.0f;
}

constexpr float k_min_bounce_speed = 0.2f;

auto bounce(float error, float speed, float restitution, float dt) -> float {
	if (restitution <= 0.0f || speed > -k_min_bounce_speed || error + (speed * dt) > 0.0f) {
		return 0.0f;
	}
	return restitution * speed;
}

auto limitTarget(float bounce_bias, float error, float inverse_dt) -> float {
	return bounce_bias < 0.0f ? bounce_bias : limitBias(error, inverse_dt);
}

void solveLinear(
    const JointRow& row, Body& a, Body& b, const glm::vec3& direction, float bias_velocity, float& accumulated, float lower,
    float upper
) {
	const float mass = linearMass(a, b, row.r_a, row.r_b, direction);
	if (mass <= 0.0f) {
		return;
	}

	const float speed = glm::dot(velocityAt(b, row.r_b) - velocityAt(a, row.r_a), direction);
	const float next = std::clamp(accumulated - ((speed + bias_velocity) * mass), lower, upper);
	const float applied = next - accumulated;
	accumulated = next;
	applyLinear(a, b, row.r_a, row.r_b, direction * applied);
}

void solveAngular(Body& a, Body& b, const glm::vec3& axis, float bias_velocity, float& accumulated, float lower, float upper) {
	const float mass = angularMass(a, b, axis);
	if (mass <= 0.0f) {
		return;
	}

	const float speed = glm::dot(b.angular_velocity - a.angular_velocity, axis);
	const float next = std::clamp(accumulated - ((speed + bias_velocity) * mass), lower, upper);
	const float applied = next - accumulated;
	accumulated = next;
	applyAngular(a, b, axis * applied);
}

auto turnInverse(const Body& body, const glm::vec3& r, const glm::vec3& axis) -> float {
	if (not movable(body)) {
		return 0.0f;
	}
	const float about_center = glm::dot(axis, body.inverse_inertia_world * axis);
	if (not(about_center > 1.0e-12f)) {
		return 0.0f;
	}
	const glm::vec3 lever = r - (axis * glm::dot(axis, r));
	const float moment = (1.0f / about_center) + (body.inverse_mass > 0.0f ? glm::dot(lever, lever) / body.inverse_mass : 0.0f);
	return std::isfinite(moment) && moment > 0.0f ? 1.0f / moment : 0.0f;
}

void applyTurn(Body& a, Body& b, const JointRow& row, const glm::vec3& axis, float impulse) {
	if (movable(a)) {
		const glm::vec3 change = axis * (impulse * turnInverse(a, row.r_a, axis));
		a.angular_velocity -= change;
		a.linear_velocity -= glm::cross(row.r_a, change);
	}
	if (movable(b)) {
		const glm::vec3 change = axis * (impulse * turnInverse(b, row.r_b, axis));
		b.angular_velocity += change;
		b.linear_velocity += glm::cross(row.r_b, change);
	}
}

void solveTurn(
    const JointRow& row, Body& a, Body& b, const glm::vec3& axis, float bias_velocity, float& accumulated, float lower,
    float upper
) {
	const float inverse = turnInverse(a, row.r_a, axis) + turnInverse(b, row.r_b, axis);
	if (not(inverse > 0.0f)) {
		return;
	}

	const float speed = glm::dot(b.angular_velocity - a.angular_velocity, axis);
	const float next = std::clamp(accumulated - ((speed + bias_velocity) / inverse), lower, upper);
	const float applied = next - accumulated;
	accumulated = next;
	applyTurn(a, b, row, axis, applied);
}

void solvePoint(const JointRow& row, Body& a, Body& b, glm::vec3& accumulated, float impulse_clamp) {
	const glm::vec3 relative = velocityAt(b, row.r_b) - velocityAt(a, row.r_a);
	glm::vec3 impulse = row.inverse_point_mass * -relative;
	if (impulse_clamp > 0.0f) {
		const float length = glm::length(impulse);
		if (length > impulse_clamp) {
			impulse *= impulse_clamp / length;
		}
	}
	accumulated += impulse;
	applyLinear(a, b, row.r_a, row.r_b, impulse);
}

// Friction in the pin holds the ends against turning with a torque that grows with the load the pin carries
void solveFriction(const JointRow& row, JointImpulses& impulses, float friction, int first_axis, Body& a, Body& b) {
	const float limit = friction * glm::length(impulses.point);
	for (int index = first_axis; index < 3; ++index) {
		solveTurn(row, a, b, row.axes_a[index], 0.0f, impulses.friction[index], -limit, limit);
	}
}

void solvePin(const JointRow& row, Joint& joint, const PinParams& params, Body& a, Body& b) {
	solvePoint(row, a, b, joint.impulses.point, params.impulse_clamp);
	solveFriction(row, joint.impulses, params.friction, 0, a, b);
}

void solveHinge(const JointRow& row, Joint& joint, const HingeParams& params, Body& a, Body& b) {
	JointImpulses& impulses = joint.impulses;
	const glm::vec3 axis = row.axes_a[2];

	if (params.motor_enabled) {
		solveTurn(
		    row, a, b, axis, -params.motor_target_velocity, impulses.motor, -params.motor_max_impulse, params.motor_max_impulse
		);
	}

	solvePoint(row, a, b, impulses.point, 0.0f);
	solveFriction(row, impulses, params.friction, 2, a, b);

	for (int index = 0; index < 2; ++index) {
		solveAngular(a, b, row.axes_a[index], 0.0f, impulses.perpendicular[index], -k_infinity, k_infinity);
	}

	if (params.limit_enabled) {
		solveTurn(
		    row,
		    a,
		    b,
		    axis,
		    limitTarget(row.bounce_lower, row.angle - params.limit_lower, row.inverse_dt),
		    impulses.limit_lower,
		    0.0f,
		    k_infinity
		);
		solveTurn(
		    row,
		    a,
		    b,
		    -axis,
		    limitTarget(row.bounce_upper, params.limit_upper - row.angle, row.inverse_dt),
		    impulses.limit_upper,
		    0.0f,
		    k_infinity
		);
	}
}

void solveSlider(const JointRow& row, Joint& joint, const SliderParams& params, Body& a, Body& b) {
	JointImpulses& impulses = joint.impulses;
	const glm::vec3 axis = row.axes_a[0];

	const glm::vec3 angular_impulse = row.inverse_angular_mass * -(b.angular_velocity - a.angular_velocity);
	impulses.angular_lock += angular_impulse;
	applyAngular(a, b, angular_impulse);

	for (int index = 0; index < 2; ++index) {
		solveLinear(row, a, b, row.axes_a[index + 1], 0.0f, impulses.perpendicular[index], -k_infinity, k_infinity);
	}

	solveLinear(
	    row,
	    a,
	    b,
	    axis,
	    limitTarget(row.bounce_lower, row.position - params.linear_limit_lower, row.inverse_dt),
	    impulses.limit_lower,
	    0.0f,
	    k_infinity
	);
	solveLinear(
	    row,
	    a,
	    b,
	    -axis,
	    limitTarget(row.bounce_upper, params.linear_limit_upper - row.position, row.inverse_dt),
	    impulses.limit_upper,
	    0.0f,
	    k_infinity
	);
}

void solveFixed(const JointRow& row, Joint& joint, Body& a, Body& b) {
	// Neither body turns relative to the other
	const glm::vec3 angular_impulse = row.inverse_angular_mass * -(b.angular_velocity - a.angular_velocity);
	joint.impulses.angular_lock += angular_impulse;
	applyAngular(a, b, angular_impulse);

	solvePoint(row, a, b, joint.impulses.point, 0.0f);
}

auto measure(const Joint& joint, const Body& body_a, const Body& body_b) -> JointRow {
	JointRow row;

	const glm::quat frame_a = body_a.rotation * joint.frame_a.rotation;
	const glm::quat frame_b = body_b.rotation * joint.frame_b.rotation;
	const glm::vec3 anchor_a = body_a.position + (body_a.rotation * joint.frame_a.position);
	const glm::vec3 anchor_b = body_b.position + (body_b.rotation * joint.frame_b.position);
	row.r_a = anchor_a - body_a.worldCenterOfMass();
	row.r_b = anchor_b - body_b.worldCenterOfMass();
	row.error = anchor_b - anchor_a;
	row.axes_a = glm::mat3_cast(frame_a);
	row.axes_b = glm::mat3_cast(frame_b);

	if (movable(body_a) || movable(body_b)) {
		const glm::mat3 skew_a = skew(row.r_a);
		const glm::mat3 skew_b = skew(row.r_b);
		const glm::mat3 point_mass = (glm::mat3 {1.0f} * (inverseMass(body_a) + inverseMass(body_b))) -
		                             (skew_a * inverseInertia(body_a) * skew_a) - (skew_b * inverseInertia(body_b) * skew_b);
		const glm::mat3 inverse = glm::inverse(point_mass);
		if (std::isfinite(inverse[0][0]) && std::isfinite(inverse[1][1]) && std::isfinite(inverse[2][2])) {
			row.inverse_point_mass = inverse;
		}
	}

	if (std::holds_alternative<SliderParams>(joint.descriptor.params) ||
	    std::holds_alternative<FixedParams>(joint.descriptor.params)) {
		row.position = glm::dot(row.error, row.axes_a[0]);

		glm::quat difference = frame_b * glm::conjugate(frame_a);
		if (difference.w < 0.0f) {
			difference = -difference;
		}
		row.angular_error = 2.0f * glm::vec3 {difference.x, difference.y, difference.z};

		if (movable(body_a) || movable(body_b)) {
			const glm::mat3 inverse = glm::inverse(inverseInertia(body_a) + inverseInertia(body_b));
			if (std::isfinite(inverse[0][0]) && std::isfinite(inverse[1][1]) && std::isfinite(inverse[2][2])) {
				row.inverse_angular_mass = inverse;
			}
		}
	}
	return row;
}

auto wrapToPi(float angle) -> float {
	if (angle > glm::pi<float>()) {
		return angle - glm::two_pi<float>();
	}
	if (angle < -glm::pi<float>()) {
		return angle + glm::two_pi<float>();
	}
	return angle;
}

auto rawHingeAngle(const JointRow& row) -> float {
	return std::atan2(glm::dot(glm::cross(row.axes_a[0], row.axes_b[0]), row.axes_a[2]), glm::dot(row.axes_a[0], row.axes_b[0]));
}

void displace(Body& body, const glm::vec3& translation, const glm::vec3& rotation_vector) {
	const glm::vec3 center = body.worldCenterOfMass() + translation;
	const glm::quat spin {0.0f, rotation_vector.x, rotation_vector.y, rotation_vector.z};
	body.rotation = glm::normalize(body.rotation + (0.5f * (spin * body.rotation)));
	body.position = center - (body.rotation * body.local_center_of_mass);
	const glm::mat3 rotation = glm::mat3_cast(body.rotation);
	body.inverse_inertia_world = rotation * body.inverse_inertia_local * glm::transpose(rotation);
}

void shiftLinear(Body& a, Body& b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& push) {
	if (movable(a)) {
		displace(a, -push * a.inverse_mass, -(a.inverse_inertia_world * glm::cross(r_a, push)));
	}
	if (movable(b)) {
		displace(b, push * b.inverse_mass, b.inverse_inertia_world * glm::cross(r_b, push));
	}
}

void shiftAngular(Body& a, Body& b, const glm::vec3& push) {
	if (movable(a)) {
		displace(a, glm::vec3 {0.0f}, -(a.inverse_inertia_world * push));
	}
	if (movable(b)) {
		displace(b, glm::vec3 {0.0f}, b.inverse_inertia_world * push);
	}
}

auto clampLength(const glm::vec3& value, float limit) -> glm::vec3 {
	const float length = glm::length(value);
	return length > limit ? value * (limit / length) : value;
}

auto clampValue(float value) -> float {
	return std::clamp(value, -k_max_position_correction, k_max_position_correction);
}

void correctLinear(const JointRow& row, Body& a, Body& b, const glm::vec3& direction, float error, float bias) {
	const float mass = linearMass(a, b, row.r_a, row.r_b, direction);
	if (mass > 0.0f) {
		shiftLinear(a, b, row.r_a, row.r_b, direction * (-bias * clampValue(error) * mass));
	}
}

void correctTurn(const JointRow& row, Body& a, Body& b, const glm::vec3& axis, float error, float bias) {
	const float inverse_a = turnInverse(a, row.r_a, axis);
	const float inverse_b = turnInverse(b, row.r_b, axis);
	const float inverse = inverse_a + inverse_b;
	if (not(inverse > 0.0f)) {
		return;
	}

	const float push = -bias * clampValue(error) / inverse;
	if (inverse_a > 0.0f) {
		const glm::vec3 turn = axis * (-push * inverse_a);
		displace(a, glm::cross(row.r_a, turn), turn);
	}
	if (inverse_b > 0.0f) {
		const glm::vec3 turn = axis * (push * inverse_b);
		displace(b, glm::cross(row.r_b, turn), turn);
	}
}

void correctAngular(Body& a, Body& b, const glm::vec3& axis, float error, float bias) {
	const float mass = angularMass(a, b, axis);
	if (mass > 0.0f) {
		shiftAngular(a, b, axis * (-bias * clampValue(error) * mass));
	}
}

}

auto makeJoint(const JointDescriptor& descriptor, const Body& body_a, const Body& body_b) -> Joint {
	const auto frame_in = [&](const Body& body) {
		const glm::quat inverse_rotation = glm::inverse(body.rotation);
		return JointFrame {
		  .position = inverse_rotation * (descriptor.anchor - body.position),
		  .rotation = inverse_rotation * descriptor.frame,
		};
	};

	Joint joint;
	joint.descriptor = descriptor;
	joint.frame_a = frame_in(body_a);
	joint.frame_b = frame_in(body_b);

	// A hinge angle is where b sits around the axis from the x of the frame, the way the limits are drawn. b is the end that
	// moves when only one does. A body centred on the axis, like a wheel, has no side to sit on and starts at zero
	if (const auto* hinge = std::get_if<HingeParams>(&descriptor.params); hinge != nullptr) {
		const glm::mat3 axes = glm::mat3_cast(descriptor.frame);
		const glm::vec3 arm = body_b.worldCenterOfMass() - descriptor.anchor;
		const float along_x = glm::dot(arm, axes[0]);
		const float along_y = glm::dot(arm, axes[1]);
		if ((along_x * along_x) + (along_y * along_y) > k_min_hinge_arm * k_min_hinge_arm) {
			joint.hinge_angle = std::atan2(along_y, along_x);
		}

		if (hinge->limit_enabled) {
			const auto outside = [&](float angle) { return std::max({hinge->limit_lower - angle, angle - hinge->limit_upper, 0.0f}); };
			const float start = joint.hinge_angle;
			for (int turns = -2; turns <= 2; ++turns) {
				const float angle = start + (static_cast<float>(turns) * glm::two_pi<float>());
				if (outside(angle) < outside(joint.hinge_angle)) {
					joint.hinge_angle = angle;
				}
			}
		}
	}
	return joint;
}

auto prepareJoint(Joint& joint, const Body& body_a, const Body& body_b, float dt) -> JointRow {
	JointRow row = measure(joint, body_a, body_b);
	row.inverse_dt = 1.0f / dt;

	if (const auto* hinge = std::get_if<HingeParams>(&joint.descriptor.params); hinge != nullptr) {
		const float raw = rawHingeAngle(row);
		joint.hinge_angle += wrapToPi(raw - joint.hinge_raw);
		joint.hinge_raw = raw;
		row.angle = joint.hinge_angle;
		if (hinge->limit_enabled) {
			const float speed = glm::dot(body_b.angular_velocity - body_a.angular_velocity, row.axes_a[2]);
			row.bounce_lower = bounce(row.angle - hinge->limit_lower, speed, hinge->limit_restitution, dt);
			row.bounce_upper = bounce(hinge->limit_upper - row.angle, -speed, hinge->limit_restitution, dt);
		}
	} else if (const auto* slider = std::get_if<SliderParams>(&joint.descriptor.params); slider != nullptr) {
		joint.slider_position = row.position;
		const float speed = glm::dot(velocityAt(body_b, row.r_b) - velocityAt(body_a, row.r_a), row.axes_a[0]);
		row.bounce_lower = bounce(row.position - slider->linear_limit_lower, speed, slider->limit_restitution, dt);
		row.bounce_upper = bounce(slider->linear_limit_upper - row.position, -speed, slider->limit_restitution, dt);
	}
	return row;
}

void warmStartJoint(const JointRow& row, const Joint& joint, Body& body_a, Body& body_b) {
	const JointImpulses& impulses = joint.impulses;
	if (std::holds_alternative<PinParams>(joint.descriptor.params)) {
		applyLinear(body_a, body_b, row.r_a, row.r_b, impulses.point);
		for (int index = 0; index < 3; ++index) {
			applyTurn(body_a, body_b, row, row.axes_a[index], impulses.friction[index]);
		}
	} else if (std::holds_alternative<HingeParams>(joint.descriptor.params)) {
		applyLinear(body_a, body_b, row.r_a, row.r_b, impulses.point);
		applyAngular(body_a, body_b, (row.axes_a[0] * impulses.perpendicular.x) + (row.axes_a[1] * impulses.perpendicular.y));
		applyTurn(
		    body_a, body_b, row, row.axes_a[2], impulses.motor + impulses.limit_lower - impulses.limit_upper + impulses.friction.z
		);
	} else if (std::holds_alternative<FixedParams>(joint.descriptor.params)) {
		applyAngular(body_a, body_b, impulses.angular_lock);
		applyLinear(body_a, body_b, row.r_a, row.r_b, impulses.point);
	} else {
		applyAngular(body_a, body_b, impulses.angular_lock);
		applyLinear(
		    body_a,
		    body_b,
		    row.r_a,
		    row.r_b,
		    (row.axes_a[1] * impulses.perpendicular.x) + (row.axes_a[2] * impulses.perpendicular.y) +
		        (row.axes_a[0] * (impulses.limit_lower - impulses.limit_upper))
		);
	}
}

void solveJoint(const JointRow& row, Joint& joint, Body& body_a, Body& body_b) {
	if (const auto* pin = std::get_if<PinParams>(&joint.descriptor.params); pin != nullptr) {
		solvePin(row, joint, *pin, body_a, body_b);
	} else if (const auto* hinge = std::get_if<HingeParams>(&joint.descriptor.params); hinge != nullptr) {
		solveHinge(row, joint, *hinge, body_a, body_b);
	} else if (const auto* slider = std::get_if<SliderParams>(&joint.descriptor.params); slider != nullptr) {
		solveSlider(row, joint, *slider, body_a, body_b);
	} else if (std::holds_alternative<FixedParams>(joint.descriptor.params)) {
		solveFixed(row, joint, body_a, body_b);
	}
}

auto jointReaction(const Joint& joint) -> JointReaction {
	const JointImpulses& impulses = joint.impulses;
	// The frames of a joint are orthonormal
	if (std::holds_alternative<HingeParams>(joint.descriptor.params)) {
		const float axial = impulses.motor + impulses.limit_lower - impulses.limit_upper + impulses.friction.z;
		return {
		  .linear = glm::length(impulses.point),
		  .angular = std::sqrt(
		      (impulses.perpendicular.x * impulses.perpendicular.x) + (impulses.perpendicular.y * impulses.perpendicular.y) +
		      (axial * axial)
		  ),
		};
	}
	if (std::holds_alternative<SliderParams>(joint.descriptor.params)) {
		const float axial = impulses.limit_lower - impulses.limit_upper;
		return {
		  .linear = std::sqrt(
		      (impulses.perpendicular.x * impulses.perpendicular.x) + (impulses.perpendicular.y * impulses.perpendicular.y) +
		      (axial * axial)
		  ),
		  .angular = glm::length(impulses.angular_lock),
		};
	}
	if (std::holds_alternative<FixedParams>(joint.descriptor.params)) {
		return {.linear = glm::length(impulses.point), .angular = glm::length(impulses.angular_lock)};
	}
	return {.linear = glm::length(impulses.point), .angular = glm::length(impulses.friction)};
}

void correctJointPositions(Joint& joint, Body& body_a, Body& body_b) {
	if (not movable(body_a) && not movable(body_b)) {
		return;
	}

	const auto close_ends = [&](float bias) {
		const JointRow row = measure(joint, body_a, body_b);
		shiftLinear(
		    body_a, body_b, row.r_a, row.r_b, row.inverse_point_mass * (clampLength(row.error, k_max_position_correction) * -bias)
		);
	};

	if (const auto* pin = std::get_if<PinParams>(&joint.descriptor.params); pin != nullptr) {
		close_ends(pin->bias);
	} else if (const auto* hinge = std::get_if<HingeParams>(&joint.descriptor.params); hinge != nullptr) {
		close_ends(hinge->bias);

		// The axes of both ends back together
		const JointRow aligned = measure(joint, body_a, body_b);
		const glm::vec3 misalignment = glm::cross(aligned.axes_a[2], aligned.axes_b[2]);
		for (int index = 0; index < 2; ++index) {
			correctAngular(body_a, body_b, aligned.axes_a[index], glm::dot(misalignment, aligned.axes_a[index]), hinge->bias);
		}

		if (hinge->limit_enabled) {
			const JointRow turned = measure(joint, body_a, body_b);
			const float angle = joint.hinge_angle + wrapToPi(rawHingeAngle(turned) - joint.hinge_raw);
			const glm::vec3 axis = turned.axes_a[2];
			if (angle < hinge->limit_lower) {
				correctTurn(turned, body_a, body_b, axis, angle - hinge->limit_lower, hinge->limit_bias);
			} else if (angle > hinge->limit_upper) {
				correctTurn(turned, body_a, body_b, -axis, hinge->limit_upper - angle, hinge->limit_bias);
			}
		}
	} else if (const auto* slider = std::get_if<SliderParams>(&joint.descriptor.params); slider != nullptr) {
		const JointRow before = measure(joint, body_a, body_b);
		shiftAngular(
		    body_a,
		    body_b,
		    before.inverse_angular_mass * (clampLength(before.angular_error, k_max_position_correction) * -slider->bias)
		);

		// Only the way along the axis is free
		const JointRow turned = measure(joint, body_a, body_b);
		for (int index = 1; index < 3; ++index) {
			const glm::vec3 direction = turned.axes_a[index];
			correctLinear(turned, body_a, body_b, direction, glm::dot(turned.error, direction), slider->bias);
		}

		const JointRow moved = measure(joint, body_a, body_b);
		if (moved.position < slider->linear_limit_lower) {
			correctLinear(moved, body_a, body_b, moved.axes_a[0], moved.position - slider->linear_limit_lower, slider->bias);
		} else if (moved.position > slider->linear_limit_upper) {
			correctLinear(moved, body_a, body_b, -moved.axes_a[0], slider->linear_limit_upper - moved.position, slider->bias);
		}
	} else if (const auto* fixed = std::get_if<FixedParams>(&joint.descriptor.params); fixed != nullptr) {
		const JointRow before = measure(joint, body_a, body_b);
		shiftAngular(
		    body_a,
		    body_b,
		    before.inverse_angular_mass * (clampLength(before.angular_error, k_max_position_correction) * -fixed->bias)
		);
		close_ends(fixed->bias);
	}
}

}
