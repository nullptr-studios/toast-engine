#include "rigidbody.hpp"

#include "box_collider.hpp"
#include "capsule_collider.hpp"
#include "collider.hpp"
#include "sphere_collider.hpp"

#include <algorithm>
#include <cmath>
#include <toast/physics/contact_events.hpp>
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
			return !capsule->disabled && std::isfinite(capsule->radius) && capsule->radius > 0.0f && std::isfinite(capsule->height) &&
			       capsule->height >= 2.0f * capsule->radius && std::isfinite(rotation_length_squared) &&
			       rotation_length_squared > 1.0e-10f;
		}
		if (const auto box = child.template as<BoxCollider>(); box.exists()) {
			const float rotation_length_squared = glm::dot(box->rotation, box->rotation);
			return !box->disabled && std::isfinite(box->size.x) && box->size.x > 0.0f && std::isfinite(box->size.y) &&
			       box->size.y > 0.0f && std::isfinite(box->size.z) && box->size.z > 0.0f && std::isfinite(rotation_length_squared) &&
			       rotation_length_squared > 1.0e-10f;
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
		listener().subscribe<event::ContactBegin>("rigidbody_contact_begin", [this](const event::ContactBegin& contact) {
			handleContactBegin(contact.contact.pair);
		});
		listener().subscribe<event::ContactEnd>("rigidbody_contact_end", [this](const event::ContactEnd& contact) {
			handleContactEnd(contact.pair);
		});
	}
}

void Rigidbody::end() {
	if (m_registration_requested) {
		m_registration_requested = false;
		listener().unsubscribe<event::ContactBegin>("rigidbody_contact_begin");
		listener().unsubscribe<event::ContactEnd>("rigidbody_contact_end");
		m_active_contacts.clear();
		Simulator::unregisterRigidbody(*this);
	}
}

void Rigidbody::handleContactBegin(const BroadPhasePair& pair) {
	BodyID other_body;
	if (pair.a.body == m_body) {
		other_body = pair.b.body;
	} else if (pair.b.body == m_body) {
		other_body = pair.a.body;
	} else {
		return;
	}

	const auto active = std::ranges::find(m_active_contacts, other_body, &ActiveContact::other_body);
	if (active != m_active_contacts.end()) {
		++active->shape_pair_count;
		return;
	}

	const toast::Box<toast::Node> other_node = Simulator::nodeFor(other_body);
	m_active_contacts.emplace_back(ActiveContact {.other_body = other_body, .other_node = other_node, .shape_pair_count = 1});
	contact_begin.fire(other_node);
}

void Rigidbody::handleContactEnd(const BroadPhasePair& pair) {
	BodyID other_body;
	if (pair.a.body == m_body) {
		other_body = pair.b.body;
	} else if (pair.b.body == m_body) {
		other_body = pair.a.body;
	} else {
		return;
	}

	const auto active = std::ranges::find(m_active_contacts, other_body, &ActiveContact::other_body);
	if (active == m_active_contacts.end()) {
		return;
	}

	if (active->shape_pair_count > 1) {
		--active->shape_pair_count;
		return;
	}

	const toast::Box<toast::Node> other_node = active->other_node;
	m_active_contacts.erase(active);
	contact_end.fire(other_node);
}

void Rigidbody::onEnable() {
	Simulator::setBodyEnabled(m_body, true);
}

void Rigidbody::onDisable() {
	Simulator::setBodyEnabled(m_body, false);
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

void Rigidbody::applyPhysicsTransform(const glm::vec3& position, const glm::quat& rotation) {
	world_position = position;
	world_rotation = rotation;
	syncTransform();
}

}
