#include "collider.hpp"

#include "box_collider.hpp"
#include "capsule_collider.hpp"
#include "dynamic_rigidbody.hpp"
#include "rigidbody.hpp"
#include "sphere_collider.hpp"

#include <cmath>
#include <toast/physics/simulator.hpp>
#include <toast/renderer/vulkan_renderer.hpp>

namespace physics {
void Collider::updateInspectorMessages() {
	static const toast::NodeMessage parent_message {
	  .severity = toast::NodeMessage::error,
	  .id = 1,
	  .text = "Colliders need to be children of a Rigidbody",
	};
	static const toast::NodeMessage invalid_geometry_message {
	  .severity = toast::NodeMessage::error,
	  .id = 3,
	  .text = "Collider dimensions must be greater than zero",
	};
	if (parent().as<Rigidbody>().exists()) {
		removeInspectorMessage(parent_message);
	} else {
		addInspectorMessage(parent_message);
	}

	bool valid_geometry = true;
	if (const auto sphere = box().as<SphereCollider>(); sphere.exists()) {
		valid_geometry = std::isfinite(sphere->radius) && sphere->radius > 0.0f;
	} else if (const auto capsule = box().as<CapsuleCollider>(); capsule.exists()) {
		const float rotation_length_squared = glm::dot(capsule->rotation, capsule->rotation);
		valid_geometry = std::isfinite(capsule->radius) && capsule->radius > 0.0f && std::isfinite(capsule->height) &&
		                 capsule->height >= 2.0f * capsule->radius && std::isfinite(rotation_length_squared) &&
		                 rotation_length_squared > 1.0e-10f;
	} else if (const auto cube = box().as<BoxCollider>(); cube.exists()) {
		const float rotation_length_squared = glm::dot(cube->rotation, cube->rotation);
		valid_geometry = std::isfinite(cube->size.x) && cube->size.x > 0.0f && std::isfinite(cube->size.y) && cube->size.y > 0.0f &&
		                 std::isfinite(cube->size.z) && cube->size.z > 0.0f && std::isfinite(rotation_length_squared) &&
		                 rotation_length_squared > 1.0e-10f;
	}

	if (valid_geometry) {
		removeInspectorMessage(invalid_geometry_message);
	} else {
		addInspectorMessage(invalid_geometry_message);
	}
}

void Collider::init() {
	m_debug_visible = enabled();
	if (renderer::VulkanRenderer::instance) {
		renderer::VulkanRenderer::instance->registerDebugDraw(this, [](toast::Node3D& node) {
			static_cast<Collider&>(node).drawDebug();
		});
	}

	// make the collider have a dependency to the rigidbody from the start
	if (auto rb = parentInternal().as<Rigidbody>(); rb.exists()) {
		addDependsOn(rb);
	}
}

void Collider::destroy() {
	if (renderer::VulkanRenderer::instance) {
		renderer::VulkanRenderer::instance->unregisterDebugDraw(this);
	}
}

void Collider::onEnable() {
	m_debug_visible = true;
	Simulator::setShapeEnabled(m_shape, not disabled);
}

void Collider::onDisable() {
	m_debug_visible = false;
	Simulator::setShapeEnabled(m_shape, false);
}

void Collider::drawDebug() {
	ZoneScoped;
	if (!m_debug_visible) {
		return;
	}
	const auto dynamic_body = parent().as<DynamicRigidbody>();
	const bool sleeping = dynamic_body.exists() && not dynamic_body->awake;
	const glm::vec4 color = disabled || sleeping ? glm::vec4(0.5f, 0.5f, 0.5f, debug_color.a) : debug_color;
	syncTransform();
	auto transform = glm::translate(glm::mat4(1.0f), world_position) * glm::mat4_cast(world_rotation);
	if (const auto sphere = box().as<SphereCollider>(); sphere.exists()) {
		glm::vec3 center = world_position;
		if (const auto body = parent().as<Rigidbody>(); body.exists()) {
			body->syncTransform();
			center = body->world_position + body->world_rotation * position;
		}
		if (!std::isfinite(sphere->radius) || sphere->radius <= 0.0f) {
			return;
		}
		renderer::debugDrawSphere(center, sphere->radius, color);
		if (debug_fill) {
			auto fill_color = color;
			fill_color.a *= 0.2f;
			renderer::debugDrawSolidSphere(center, sphere->radius, fill_color);
		}
	} else if (const auto capsule = box().as<CapsuleCollider>(); capsule.exists()) {
		renderer::debugDrawCapsule(transform, capsule->radius, capsule->height, color, debug_fill);
	} else if (const auto cube = box().as<BoxCollider>(); cube.exists()) {
		renderer::debugDrawShapeBox(glm::scale(transform, cube->size), color, debug_fill);
	}
}
}
