/**
 * @file RigidbodyComponent.hpp
 * @author Iñaki
 * @date 08/10/2025
 * @brief Object that is subject to physics
 */

#pragma once

#include "../Toast/Components/Component.hpp"
#include "../Toast/Components/TransformComponent.hpp"
#include "Colliders/Collider.hpp"

namespace physics {

/// @brief Component that represents an object that is subject to physics
class RigidbodyComponent : public toast::Component {
	friend class PhysicsSystem;

public:
	REGISTER_TYPE(RigidbodyComponent);

	void Init() override;
	void Begin() override;
	void PhysTick() override;
	void Destroy() override;

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;
#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	enum Type : unsigned char {
		Static,
		Kinematic,
		Dynamic,
		Count
	};

	static constexpr const char* TypeNames[] = { "Static", "Kinematic", "Dynamic" };

	Type rigidbodyType { Dynamic };
	float mass = 1.0f;
	glm::vec2 centerOfMass = { 0.0f, 0.0f };
	glm::vec2 drag = { 0.1f, 0.12f };
	float angularDrag = 0.05f;
	glm::vec2 gravityScale = { 0.0f, 1.0f };
	glm::vec2 terminalVelocity = { 1e3f, 1e2f };

	[[nodiscard]]
	ICollider* collider() const noexcept {
		return m_collider;
	}

	void AddForce(glm::vec2 value);
	void AddForce(glm::vec2 value, glm::vec3 position);
	void AddTorque(float value);
	void AddAcceleration(glm::vec2 value);

	// Rewrite
	void SetVelocity(glm::vec2 value) {
		m_alexeyVelocity = value;
	}

	void SetHorizontalVelocity(float value) {
		m_alexeyVelocity.x = value;
	}

	// Lowkey need this for movement -a
	[[nodiscard]]
	glm::vec2 acceleration() const {
		return m_acceleration;
	}

	[[nodiscard]]
	glm::vec2 velocity() const {
		return m_alexeyVelocity;
	}

	[[nodiscard]]
	glm::vec2 true_velocity() const {
		return m_velocity;
	}

private:
	ICollider* m_collider {};
	glm::vec2 m_acceleration = { 0.0f, 0.0f };
	glm::vec2 m_velocity = { 0.0f, 0.0f };
	glm::vec2 m_alexeyVelocity = { 0.0f, 0.0f };
	glm::vec2 m_velocityEpsilon = { 0.01, 0.01 };
	float m_angularAcceleration = { 0.0f };
	float m_angularVelocity = { 0.0f };

	toast::TransformComponent* m_transform = nullptr;
};

}
