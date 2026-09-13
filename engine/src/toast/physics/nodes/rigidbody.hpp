/**
 * @file Rigidbody.hpp
 * @author Xein
 * @date 09 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "../physics_material.hpp"
#include "../body.hpp"
#include "../shape.hpp"

#include <toast/world/node_3d.hpp>
#include <vector>

namespace physics {

class Simulator;

class [[ToastNode, Hidden, Interface, Icon("PhysicsBody"), Color("Green")]] TOAST_API Rigidbody : public toast::Node3D {
	friend class Simulator;

protected:
	explicit Rigidbody(BodyType type) : m_body_type(type) { }
	virtual void configureBodyDescriptor(BodyDescriptor& descriptor) const { }
	
	[[Reflect]]
	assets::Handle<assets::PhysicsMaterial> material;

	[[Reflect, Name("Lock X"), Group("Position Locks"), ReadOnly]]
	bool lock_pos_x = false;
	[[Reflect, Name("Lock Y"), Group("Position Locks"), ReadOnly]]
	bool lock_pos_y = false;
	[[Reflect, Name("Lock Z"), Group("Position Locks"), ReadOnly]]
	bool lock_pos_z = false;
	[[Reflect, Name("Lock X"), Group("Rotation Locks"), ReadOnly]]
	bool lock_rot_x = false;
	[[Reflect, Name("Lock Y"), Group("Rotation Locks"), ReadOnly]]
	bool lock_rot_y = false;
	[[Reflect, Name("Lock Z"), Group("Rotation Locks"), ReadOnly]]
	bool lock_rot_z = false;

private:
	void updateInspectorMessages() override;
	void begin();
	void end();

	[[nodiscard]]
	auto descriptor() const -> BodyDescriptor;
	[[nodiscard]]
	auto sphereShapes() const -> std::vector<SphereShape>;
	[[nodiscard]]
	auto boxShapes() const -> std::vector<BoxShape>;
	[[nodiscard]]
	auto capsuleShapes() const -> std::vector<CapsuleShape>;

	void assignBody(BodyID body) noexcept { m_body = body; }

	void applyPhysicsTransform(const glm::vec3& position, const glm::quat& rotation);

	BodyType m_body_type;
	BodyID m_body;
	bool m_registration_requested = false;
};

}
