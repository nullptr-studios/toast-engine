#include "debug_visuals.hpp"

#include "physics_settings.hpp"
#include "simulator.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <toast/renderer/debug/debug_draw.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <tracy/Tracy.hpp>

namespace physics {
namespace {

const glm::vec4 k_fixed {0.55F, 0.57F, 0.62F, 0.9F};
const glm::vec4 k_kinematic {1.0F, 0.85F, 0.2F, 0.95F};
const glm::vec4 k_awake {0.2F, 1.0F, 0.4F, 0.95F};
const glm::vec4 k_asleep {0.3F, 0.55F, 1.0F, 0.95F};
const glm::vec4 k_contact {1.0F, 0.3F, 0.3F, 1.0F};
const glm::vec4 k_bounds {0.9F, 0.9F, 0.9F, 0.35F};
const glm::vec4 k_velocity {0.3F, 0.9F, 1.0F, 1.0F};
const glm::vec4 k_center {1.0F, 0.6F, 0.1F, 1.0F};

}

auto debugColor(const Body& body) -> glm::vec4 {
	glm::vec4 color = k_fixed;
	switch (body.type) {
		case BodyType::static_body: color = k_fixed; break;
		case BodyType::kinematic_body: color = k_kinematic; break;
		case BodyType::dynamic_body: color = body.awake ? k_awake : k_asleep; break;
	}
	if (!body.enabled) {
		color.a *= 0.3F;
	}
	return color;
}

void Simulator::drawDebugVisuals() const {
	const PhysicsParams& params = tunables();
	const bool wanted =
	    params.draw_shapes || params.draw_bounds || params.draw_contacts || params.draw_velocity || params.draw_centers;
	if (!wanted || renderer::VulkanRenderer::instance == nullptr) {
		return;
	}
	ZoneScopedN("physics::DrawDebug");

	if (!renderer::VulkanRenderer::instance->debugDrawEnabled()) {
		renderer::VulkanRenderer::instance->setDebugDrawEnabled(true);
	}

	const auto body_of = [this](const Shape& shape) -> const Body* {
		if (shape.owner.slot >= m_bodies.size() || !m_bodies[shape.owner.slot].occupied) {
			return nullptr;
		}
		return &m_bodies[shape.owner.slot].body;
	};

	for (const ShapeSlot& slot : m_shapes) {
		if (!slot.occupied || !slot.shape.enabled) {
			continue;
		}
		const Shape& shape = slot.shape;
		const Body* body = body_of(shape);
		if (body == nullptr) {
			continue;
		}
		const glm::vec4 color = debugColor(*body);

		if (params.draw_bounds) {
			const AABB bounds = worldShapeBounds(*body, shape);
			debug::drawBox(bounds.min, bounds.max, k_bounds);
		}

		if (params.draw_shapes) {
			const glm::vec3 center = body->position + (body->rotation * shape.getLocalCenter());
			const glm::quat rotation = glm::normalize(body->rotation * shape.getLocalRotation());
			const glm::mat4 placed = glm::translate(glm::mat4(1.0F), center) * glm::mat4_cast(rotation);
			switch (shape.type) {
				case ShapeType::sphere: debug::drawSphere(center, shape.sphere.radius, color, 16); break;
				case ShapeType::box: debug::drawShapeBox(glm::scale(placed, shape.box.size), color, false); break;
				case ShapeType::capsule: debug::drawCapsule(placed, shape.capsule.radius, shape.capsule.height, color, false); break;
				case ShapeType::voxel: {
					const glm::vec3 middle = 0.5F * (shape.voxel.local_bounds.min + shape.voxel.local_bounds.max);
					const glm::vec3 half = 0.5F * (shape.voxel.local_bounds.max - shape.voxel.local_bounds.min);
					debug::drawOrientedBox(glm::translate(placed, middle), half, color);
					break;
				}
			}
		}
	}

	if (params.draw_centers || params.draw_velocity) {
		const float scale = params.velocity_scale;
		for (const BodySlot& slot : m_bodies) {
			if (!slot.occupied || !slot.body.enabled || slot.body.type == BodyType::static_body) {
				continue;
			}
			const Body& body = slot.body;
			const glm::vec3 center = body.worldCenterOfMass();
			if (params.draw_centers) {
				debug::drawSphere(center, 0.04F, k_center, 8);
			}
			if (params.draw_velocity && body.awake && glm::dot(body.linear_velocity, body.linear_velocity) > 1.0e-6F) {
				debug::drawArrow(center, center + (body.linear_velocity * scale), k_velocity, 0.08F);
			}
		}
	}

	if (params.draw_contacts) {
		const float length = params.contact_normal_length;
		for (const Manifold& manifold : m_manifolds) {
			for (size_t i = 0; i < manifold.contact_count && i < manifold.contacts.size(); ++i) {
				const glm::vec3 point = manifold.contacts[i].position;
				debug::drawSphere(point, 0.03F, k_contact, 8);
				debug::drawLine(point, point + (manifold.normal * length), k_contact);
			}
		}
	}
}

}
