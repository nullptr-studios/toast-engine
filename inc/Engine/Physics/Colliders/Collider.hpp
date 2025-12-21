/**
 * @file Collider.hpp
 * @author Iñaki
 * @date 08/10/2025
 *
 * @brief Contains the base collider class
 */
#pragma once
#include "Engine/Physics/PrimitiveCollisions.hpp"

#include <Engine/Toast/Components/Component.hpp>
#include <glm/vec4.hpp>

namespace physics {

class RigidbodyComponent;

enum class ColliderFlags : unsigned char {
	None = 0b0000,
	Default = 0b0001,
	Ground = 0b0010,
	Enemy = 0b0100,
	Player = 0b1000
};

inline ColliderFlags operator|(ColliderFlags a, ColliderFlags b) {
	using T = std::underlying_type_t<ColliderFlags>;
	return static_cast<ColliderFlags>(static_cast<T>(a) | static_cast<T>(b));
}

inline ColliderFlags operator&(ColliderFlags a, ColliderFlags b) {
	using T = std::underlying_type_t<ColliderFlags>;
	return static_cast<ColliderFlags>(static_cast<T>(a) & static_cast<T>(b));
}

inline ColliderFlags operator~(ColliderFlags a) {
	using T = std::underlying_type_t<ColliderFlags>;
	return static_cast<ColliderFlags>(~static_cast<T>(a));
}

// Compound assignment operators (optional but convenient)
inline ColliderFlags& operator|=(ColliderFlags& a, ColliderFlags b) {
	a = a | b;
	return a;
}

inline ColliderFlags& operator&=(ColliderFlags& a, ColliderFlags b) {
	a = a & b;
	return a;
}

// Utility: check if any bit is set
inline bool any(ColliderFlags f) {
	using T = std::underlying_type_t<ColliderFlags>;
	return static_cast<T>(f) != 0;
}

/// @brief Interface that handles the colliders
class ICollider : public toast::Component {
	friend class PhysicsSystem;
	friend class RigidbodyComponent;

public:
	REGISTER_ABSTRACT(ICollider);
	enum class ColliderType : unsigned char {
		Circle,
		Box,
		Mesh
	};

	bool trigger = false;
	/// @brief  Getter for the collider type
	[[nodiscard]]
	virtual constexpr ColliderType collider_type() const = 0;

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;
#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	/// @typedef EnterCallback
	/// Lambda that has the "other" object and the "ContactInfo" as parameters
	using EnterCallback = std::function<void(Object*, ContactInfo)>;
	using ExitCallback = std::function<void(Object*)>;

	void OnCollisionEnter(EnterCallback&& function);
	void OnCollisionExit(ExitCallback&& function);

	ColliderFlags flags = ColliderFlags::Default | ColliderFlags::Ground;

	bool debug = false;

	[[nodiscard]]
	glm::vec4 color() const {
		return m_color;
	}

	void color(const glm::vec4 collider_color) {
		m_color = collider_color;
	}

	[[nodiscard]]
	bool is_colliding() const;
	bool is_colliding_with(unsigned id) const;

protected:
	void CallOnCollisionEnter(Object* other, const ContactInfo& contact) const;
	void CallOnCollisionExit(Object* other) const;
	EnterCallback m_onCollisionEnter;
	ExitCallback m_onCollisionExit;

private:
	glm::vec4 m_color { 0, 0, 1, 1 };
	std::list<unsigned> m_collidingStack;
	std::optional<RigidbodyComponent*> m_rigidbody;
};

}
