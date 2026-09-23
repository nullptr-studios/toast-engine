/**
 * @file Rigidbody.hpp
 * @author Xein
 * @date 09 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "../body.hpp"
#include "../collision.hpp"
#include "../physics_material.hpp"
#include "../shape.hpp"

#include <toast/world/node_3d.hpp>
#include <vector>

namespace physics {

class Simulator;

class [[ToastNode, Hidden, Interface, Icon("PhysicsBody"), Color("Green")]] TOAST_API Rigidbody : public toast::Node3D {
	friend class Simulator;

public:
	signals::Signal<toast::Box<toast::Node>> contact_begin;
	signals::Signal<toast::Box<toast::Node>> contact_end;

protected:
	explicit Rigidbody(BodyType type) : m_body_type(type) { }

	virtual void configureBodyDescriptor(BodyDescriptor& descriptor) const { }

	[[nodiscard]]
	auto bodyID() const noexcept -> BodyID {
		return m_body;
	}

	[[Reflect]]
	assets::Handle<assets::PhysicsMaterial> material;

	[[Reflect, Name("Lock X"), Group("Position Locks")]]
	bool lock_pos_x = false;
	[[Reflect, Name("Lock Y"), Group("Position Locks")]]
	bool lock_pos_y = false;
	[[Reflect, Name("Lock Z"), Group("Position Locks")]]
	bool lock_pos_z = false;
	[[Reflect, Name("Lock X"), Group("Rotation Locks")]]
	bool lock_rot_x = false;
	[[Reflect, Name("Lock Y"), Group("Rotation Locks")]]
	bool lock_rot_y = false;
	[[Reflect, Name("Lock Z"), Group("Rotation Locks")]]
	bool lock_rot_z = false;

private:
	struct ActiveContact {
		BodyID other_body;
		toast::Box<toast::Node> other_node;
		uint32_t shape_pair_count = 0;
	};

	void updateInspectorMessages() override;
	void begin();
	void end();
	void onEnable();
	void onDisable();
	void handleContactBegin(const BroadPhasePair& pair);
	void handleContactEnd(const BroadPhasePair& pair);

	[[nodiscard]]
	auto descriptor() const -> BodyDescriptor;

	void assignBody(BodyID body) noexcept { m_body = body; }

	void applyPhysicsTransform(const glm::vec3& position, const glm::quat& rotation);

	void onEditorTransformChanged() override;

	BodyType m_body_type;
	BodyID m_body;
	std::vector<ActiveContact> m_active_contacts;
	bool m_registration_requested = false;
};

}
