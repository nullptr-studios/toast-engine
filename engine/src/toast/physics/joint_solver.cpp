#include "joint_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace physics::_detail {

namespace {

constexpr float k_max_correction_speed = 20.0f;
constexpr float k_infinity = std::numeric_limits<float>::infinity();

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

auto biasVelocity(float error, float bias, float inverse_dt) -> float {
	return std::clamp(bias * error * inverse_dt, -k_max_correction_speed, k_max_correction_speed);
}

auto biasVelocity(const glm::vec3& error, float bias, float inverse_dt) -> glm::vec3 {
	const glm::vec3 velocity = error * (bias * inverse_dt);
	const float length = glm::length(velocity);
	return length > k_max_correction_speed ? velocity * (k_max_correction_speed / length) : velocity;
}

auto limitBias(float error, float bias, float inverse_dt) -> float {
	return error > 0.0f ? error * inverse_dt : biasVelocity(error, bias, inverse_dt);
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

void solvePoint(const JointRow& row, Body& a, Body& b, glm::vec3& accumulated, float bias, float damping, float impulse_clamp) {
	const glm::vec3 relative = velocityAt(b, row.r_b) - velocityAt(a, row.r_a);
	glm::vec3 impulse = row.inverse_point_mass * -((relative * damping) + biasVelocity(row.error, bias, row.inverse_dt));
	if (impulse_clamp > 0.0f) {
		const float length = glm::length(impulse);
		if (length > impulse_clamp) {
			impulse *= impulse_clamp / length;
		}
	}
	accumulated += impulse;
	applyLinear(a, b, row.r_a, row.r_b, impulse);
}

void solvePin(const JointRow& row, Joint& joint, const PinParams& params, Body& a, Body& b) {
	solvePoint(row, a, b, joint.impulses.point, params.bias, params.damping, params.impulse_clamp);
}

void solveHinge(const JointRow& row, Joint& joint, const HingeParams& params, Body& a, Body& b) {
	JointImpulses& impulses = joint.impulses;
	const glm::vec3 axis = row.axes_a[2];

	if (params.motor_enabled) {
		const float mass = angularMass(a, b, axis);
		if (mass > 0.0f) {
			const float speed = glm::dot(b.angular_velocity - a.angular_velocity, axis);
			const float next = std::clamp(
			    impulses.motor - ((speed - params.motor_target_velocity) * mass), -params.motor_max_impulse, params.motor_max_impulse
			);
			applyAngular(a, b, axis * (next - impulses.motor));
			impulses.motor = next;
		}
	}

	solvePoint(row, a, b, impulses.point, params.bias, 1.0f, 0.0f);

	// The axes of both ends stay together
	const glm::vec3 misalignment = glm::cross(axis, row.axes_b[2]);
	for (int index = 0; index < 2; ++index) {
		const glm::vec3 direction = row.axes_a[index];
		solveAngular(
		    a,
		    b,
		    direction,
		    biasVelocity(glm::dot(misalignment, direction), params.bias, row.inverse_dt),
		    impulses.perpendicular[index],
		    -k_infinity,
		    k_infinity
		);
	}

	if (params.limit_enabled) {
		solveAngular(
		    a,
		    b,
		    axis,
		    limitBias(row.angle - params.limit_lower, params.limit_bias, row.inverse_dt),
		    impulses.limit_lower,
		    0.0f,
		    k_infinity
		);
		solveAngular(
		    a,
		    b,
		    -axis,
		    limitBias(params.limit_upper - row.angle, params.limit_bias, row.inverse_dt),
		    impulses.limit_upper,
		    0.0f,
		    k_infinity
		);
	}
}

void solveSlider(const JointRow& row, Joint& joint, const SliderParams& params, Body& a, Body& b) {
	JointImpulses& impulses = joint.impulses;
	const glm::vec3 axis = row.axes_a[0];

	const glm::vec3 angular_impulse = row.inverse_angular_mass * -((b.angular_velocity - a.angular_velocity) +
	                                                               biasVelocity(row.angular_error, params.bias, row.inverse_dt));
	impulses.angular_lock += angular_impulse;
	applyAngular(a, b, angular_impulse);

	for (int index = 0; index < 2; ++index) {
		const glm::vec3 direction = row.axes_a[index + 1];
		solveLinear(
		    row,
		    a,
		    b,
		    direction,
		    biasVelocity(glm::dot(row.error, direction), params.bias, row.inverse_dt),
		    impulses.perpendicular[index],
		    -k_infinity,
		    k_infinity
		);
	}

	solveLinear(
	    row,
	    a,
	    b,
	    axis,
	    limitBias(row.position - params.linear_limit_lower, params.bias, row.inverse_dt),
	    impulses.limit_lower,
	    0.0f,
	    k_infinity
	);
	solveLinear(
	    row,
	    a,
	    b,
	    -axis,
	    limitBias(params.linear_limit_upper - row.position, params.bias, row.inverse_dt),
	    impulses.limit_upper,
	    0.0f,
	    k_infinity
	);
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
	return joint;
}

auto prepareJoint(Joint& joint, const Body& body_a, const Body& body_b, float dt) -> JointRow {
	JointRow row;
	row.inverse_dt = 1.0f / dt;

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

	if (const auto* hinge = std::get_if<HingeParams>(&joint.descriptor.params); hinge != nullptr) {
		const glm::vec3 axis = row.axes_a[2];
		const float raw =
		    std::atan2(glm::dot(glm::cross(row.axes_a[0], row.axes_b[0]), axis), glm::dot(row.axes_a[0], row.axes_b[0]));
		float delta = raw - joint.hinge_raw;
		if (delta > glm::pi<float>()) {
			delta -= glm::two_pi<float>();
		} else if (delta < -glm::pi<float>()) {
			delta += glm::two_pi<float>();
		}
		joint.hinge_raw = raw;
		joint.hinge_angle += delta;
		row.angle = joint.hinge_angle;
	} else if (std::holds_alternative<SliderParams>(joint.descriptor.params)) {
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

void warmStartJoint(const JointRow& row, const Joint& joint, Body& body_a, Body& body_b) {
	const JointImpulses& impulses = joint.impulses;
	if (std::holds_alternative<PinParams>(joint.descriptor.params)) {
		applyLinear(body_a, body_b, row.r_a, row.r_b, impulses.point);
	} else if (std::holds_alternative<HingeParams>(joint.descriptor.params)) {
		applyLinear(body_a, body_b, row.r_a, row.r_b, impulses.point);
		applyAngular(
		    body_a,
		    body_b,
		    (row.axes_a[0] * impulses.perpendicular.x) + (row.axes_a[1] * impulses.perpendicular.y) +
		        (row.axes_a[2] * (impulses.motor + impulses.limit_lower - impulses.limit_upper))
		);
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
	}
}

}
