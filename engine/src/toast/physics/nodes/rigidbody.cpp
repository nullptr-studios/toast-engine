#include "rigidbody.hpp"

#include "collider.hpp"
#include "sphere_collider.hpp"
#include "box_collider.hpp"
#include "capsule_collider.hpp"

#include <algorithm>
#include <cmath>
#include <toast/physics/simulator.hpp>

namespace physics {

void Rigidbody::updateInspectorMessages() {
	static const toast::NodeMessage message {
		.severity = toast::NodeMessage::warning,
		.id = 2,
		.text = "Rigidbodies require a collider",
	};

	const bool has_collider = std::ranges::any_of(children(), [](const auto& child) {
		if (const auto sphere = child.template as<SphereCollider>(); sphere.exists()) {
			return !sphere->disabled && std::isfinite(sphere->radius) && sphere->radius > 0.0f;
		}
		if (const auto capsule = child.template as<CapsuleCollider>(); capsule.exists()) {
			const float rotation_length_squared = glm::dot(capsule->rotation, capsule->rotation);
			return !capsule->disabled && std::isfinite(capsule->radius) && capsule->radius > 0.0f &&
			       std::isfinite(capsule->height) && capsule->height >= 2.0f * capsule->radius &&
			       std::isfinite(rotation_length_squared) && rotation_length_squared > 1.0e-10f;
		}
		if (const auto box = child.template as<BoxCollider>(); box.exists()) {
			const float rotation_length_squared = glm::dot(box->rotation, box->rotation);
			return !box->disabled && std::isfinite(box->size.x) && box->size.x > 0.0f && std::isfinite(box->size.y) &&
			       box->size.y > 0.0f && std::isfinite(box->size.z) && box->size.z > 0.0f &&
			       std::isfinite(rotation_length_squared) && rotation_length_squared > 1.0e-10f;
		}
		return false;
	});
	if (has_collider) {
		removeInspectorMessage(message);
	} else {
		addInspectorMessage(message);
	}
}

void Rigidbody::begin() {
	if (not m_registration_requested && participatesIn(toast::NodeOwnerParticipation::gameplay_tick)) {
		m_registration_requested = true;
		Simulator::registerRigidbody(*this);
	}
}

void Rigidbody::end() {
	if (m_registration_requested) {
		m_registration_requested = false;
		Simulator::unregisterRigidbody(*this);
	}
}

auto Rigidbody::descriptor() const -> BodyDescriptor {
	syncTransform();
	BodyDescriptor result;
	result.type = m_body_type;
	result.position = world_position;
	result.rotation = world_rotation;
	configureBodyDescriptor(result);
	return result;
}

auto Rigidbody::sphereShapes() const -> std::vector<SphereShape> {
	std::vector<SphereShape> shapes;

	for (const auto& child : children()) {
		const auto sphere = child.as<SphereCollider>();
		if (not sphere.exists() || sphere->disabled) {
			continue;
		}

		shapes.emplace_back(SphereShape {
			.local_center = sphere->position,
			.radius = sphere->radius,
		});
	}

	return shapes;
}

auto Rigidbody::boxShapes() const -> std::vector<BoxShape> {
	std::vector<BoxShape> shapes;

	for (const auto& child : children()) {
		const auto box = child.as<BoxCollider>();
		if (!box.exists() || box->disabled) {
			continue;
		}

		shapes.emplace_back(BoxShape {
			.local_center = box->position,
			.local_rotation = box->rotation,
			.size = box->size,
		});
	}

	return shapes;
}

auto Rigidbody::capsuleShapes() const -> std::vector<CapsuleShape> {
	std::vector<CapsuleShape> shapes;

	for (const auto& child : children()) {
		const auto capsule = child.as<CapsuleCollider>();
		if (!capsule.exists() || capsule->disabled) {
			continue;
		}

		shapes.emplace_back(CapsuleShape {
			.local_center = capsule->position,
			.local_rotation = capsule->rotation,
			.radius = capsule->radius,
			.height = capsule->height,
		});
	}

	return shapes;
}

void Rigidbody::applyPhysicsTransform(const glm::vec3& position, const glm::quat& rotation) {
	world_position = position;
	world_rotation = rotation;
	syncTransform();
}

}
