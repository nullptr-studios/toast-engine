#include "PhysicsSystem.hpp"

#include "../../inc/Engine/Core/Log.hpp"
#include "../../inc/Engine/Core/Time.hpp"
#include "Engine/Physics/PrimitiveCollisions.hpp"
#include "Engine/Renderer/DebugDrawLayer.hpp"
#include "Engine/Toast/Engine.hpp"
#include "Engine/Toast/Objects/Scene.hpp"
#include "Engine/Toast/SimulateWorldEvent.hpp"
#include "Engine/Toast/World.hpp"

namespace physics {

PhysicsSystem* PhysicsSystem::m_instance = nullptr;

PhysicsSystem::PhysicsSystem() {
	if (m_instance) {
		throw ToastException("Trying to create a physics system when one already exists");
	}
	m_instance = this;

	m_listener = new event::ListenerComponent();

	std::thread([this] {
		TOAST_INFO("Creating Physics thread");

#ifdef TOAST_EDITOR
		// We subscribe an event to check if we should simulate the world or not
		// Leave the priority as is
		m_listener->Subscribe<toast::SimulateWorldEvent>([this](const toast::SimulateWorldEvent* e) {
			m_simulateWorld = e->value;
			return false;    // This shouldn't consume the event
		});
#endif

		// Physics loop
		while (!toast::Engine::GetInstance()->GetShouldClose()) {
			Time::GetInstance()->PhysTick();

			this->Tick();

			// framerate controller
			{
				PROFILE_ZONE_NC("physics::Wait()", 0x404040);
				while (Time::GetInstance()->fixed_delta_t() < m_targetDelta) { }
			}
		}

		// Logic for destroying physics
		TOAST_INFO("Destroying Physics System");
		// this ->Destroy();
		// exit(0);
	}).detach();
}

PhysicsSystem* PhysicsSystem::GetInstance() {
	if (!m_instance) {
		throw ToastException("Physics System has not been created yet");
	}
	return m_instance;
}

const std::deque<RigidbodyComponent*>& PhysicsSystem::GetRigidBodies() const {
	return m_rigidbodies;
}

void PhysicsSystem::AddRigidbody(RigidbodyComponent* rigidbody) {
	if (std::ranges::find(m_rigidbodies, rigidbody) != m_rigidbodies.end()) {
		return;
	}
	m_rigidbodies.push_back(rigidbody);
}

void PhysicsSystem::RemoveRigidbody(const RigidbodyComponent* rigidbody) {
	const auto it = std::ranges::find(m_rigidbodies, rigidbody);
	if (it == m_rigidbodies.end()) {
		TOAST_WARN("Tried to remove a rigidbody that does not exist in the physics system");
		return;
	}
	m_rigidbodies.erase(it);
}

const std::deque<ICollider*>& PhysicsSystem::GetColliders() const {
	return m_colliders;
}

void PhysicsSystem::AddCollider(ICollider* collider) {
	if (std::ranges::find(m_colliders, collider) != m_colliders.end()) {
		return;
	}
	m_colliders.push_back(collider);
}

void PhysicsSystem::RemoveCollider(const ICollider* collider) {
	const auto it = std::ranges::find(m_colliders, collider);
	if (it == m_colliders.end()) {
		TOAST_WARN("Tried to remove a collider that does not exist in the physics system");
		return;
	}
	m_colliders.erase(it);
}

glm::vec2 PhysicsSystem::gravity() {
	return GetInstance()->m_gravity;
}

void PhysicsSystem::gravity(glm::vec2 multiplier) {
	GetInstance()->m_gravity = multiplier;
}

void PhysicsSystem::SetTickRate(const float value) {
	GetInstance()->m_targetDelta = 1 / value;
}

void PhysicsSystem::RequestHaltSimulation() {
	GetInstance()->m_requestReceived = true;
}

bool PhysicsSystem::WaitForAnswer() {
	return GetInstance()->m_requestAnswered;
}

void PhysicsSystem::ReceivedAnswer() {
	GetInstance()->m_requestAnswered = false;
	GetInstance()->m_requestReceived = false;
}

void PhysicsSystem::Tick() {
	if (!m_simulateWorld) {
		return;
	}

	if (m_requestReceived) {
		m_requestAnswered = true;
		return;
	}

	PROFILE_ZONE;

	// Tick objects
	toast::World::Instance()->PhysTick();

	for (const auto& rb1 : m_rigidbodies) {
		PROFILE_ZONE_N("Outer");
#ifdef _DEBUG
		const char* rb1_name = rb1->parent()->name().c_str();
		PROFILE_TEXT(rb1_name, strlen(rb1_name));
#endif

		// Check if the rigidbody has run begin
		if (!rb1->has_run_begin()) {
			TOAST_WARN("Tried to simulate physics but Rigidbody in \"{0}\" hasn't run the begin", rb1->parent()->name());
			continue;
		}

		auto* c1 = rb1->collider();

		if (!rb1->enabled() || !c1->enabled()) {
			continue;
		}
		if (rb1->rigidbodyType != RigidbodyComponent::Dynamic) {
			continue;
		}

		for (const auto& c2 : m_colliders) {
			PROFILE_ZONE_N("Inner");
#ifdef _DEBUG
			const char* c2_name = c2->parent()->name().c_str();
			PROFILE_TEXT(c2_name, strlen(c2_name));
#endif

			if (!c2->has_run_begin()) {
				TOAST_WARN("Tried to simulate physics but Collider in \"{0}\" hasn't run the begin", c2->parent()->name());
				continue;
			}

			auto rb2 = c2->m_rigidbody;
			if (rb2.has_value()) {
				if (!(*rb2)->has_run_begin()) {
					TOAST_WARN("Tried to simulate physics but Rigidbody in \"{0}\" hasn't run the begin", rb1->parent()->name());
					continue;
				}
			}

			if (c2 == c1) {
				continue;
			}
			if (!c2->enabled()) {
				continue;
			}

			if ((static_cast<char>(c1->flags) & static_cast<char>(c2->flags)) == 0) {
				continue;
			}

			// Collision has happened
			if (const auto contact = Collide(c1, c2); contact) {
				// Call the lambdas only the first time
				auto& c1_stack = c1->m_collidingStack;
				auto& c2_stack = c2->m_collidingStack;
				if (std::ranges::find(c1_stack, c2->id()) == c1_stack.end()) {
#ifdef TOAST_EDITOR
					c1->color(glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
					c2->color(glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
#endif

					c1_stack.emplace_back(c2->id());
					c2_stack.emplace_back(c1->id());
					c1->CallOnCollisionEnter(c2->parent(), *contact);
					c2->CallOnCollisionEnter(c1->parent(), *contact);
				}

				if (!c1->trigger && !c2->trigger) {
					// Collision resolution
					if (!rb2.has_value() || (*rb2)->rigidbodyType != RigidbodyComponent::Dynamic) {
						// Resolve without mass into consideration (only moves rb 1)
						rb1->m_transform->position(
						    rb1->m_transform->position() -
						    glm::vec3(contact->normal.x * contact->penetration * 7.01f, contact->normal.y * contact->penetration * 7.01f, 0.0f)
						);
						rb1->m_velocity.y = 0.0f;
						rb1->m_velocity.x = 0.0f;
						rb1->m_alexeyVelocity.x = 0.0f;
						rb1->m_alexeyVelocity.y = 0.0f;
						// renderer::DebugLine(glm::vec2(rb1->m_transform->position().x, rb1->m_transform->position().y), contact->intersection, glm::vec4(1.0f,
						// 0.0f, 0.0f, 1.0f));
					}

					// if (rb2 != std::nullopt && (*rb2)->rigidbodyType == RigidbodyComponent::Dynamic) {
					//	rb1->AddForce(contact->normal * (rb1->gravityScale * gravity() + rb1->m_velocity * rb1->m_alexeyVelocity) * rb1->mass * 0.5f);
					//	rb1->m_velocity.y = 0.0f;
					//	rb1->m_velocity.x = 0.0f;
					//	(*rb2)->AddForce(contact->normal * (rb1->gravityScale * gravity() + (*rb2)->m_velocity * (*rb2)->m_alexeyVelocity) * (*rb2)->mass *
					// 0.5f);
					//	(*rb2)->m_velocity.y = 0.0f;
					//	(*rb2)->m_velocity.x = 0.0f;
					// }
				}

				continue;
			}

			// Collision has not happened
			// Call the lambdas only the first time
			auto& c1_stack = c1->m_collidingStack;
			auto& c2_stack = c2->m_collidingStack;

			// sometimes it crashes around here
			if (std::ranges::find(c1_stack, c2->id()) != c1_stack.end()) {
#ifdef TOAST_EDITOR
				c1->color(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
				c2->color(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
#endif

				c1_stack.remove(c2->id());
				c2_stack.remove(c1->id());
				c1->CallOnCollisionExit(c1->parent());
				c2->CallOnCollisionExit(c1->parent());
			}
		}
	}
}

void PhysicsSystem::Destroy() {
	// Destroy the listenerComponent manually since the thread is not really destroyed ever
	// so the uptr wouldn't get off scope ever
	delete m_listener;
}

}
