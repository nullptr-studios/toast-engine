/// @file Rigidbody.hpp
/// @author Xein
/// @date 22 Dec 2025

#pragma once
#include <Engine/Toast/Components/Component.hpp>
#include <glm/glm.hpp>

namespace physics {

class Rigidbody : public toast::Component {
public:
	REGISTER_TYPE(Rigidbody);
	glm::vec2 velocity = { 10.0f, 10.0f };
	glm::vec2 bounds = { 20.0f, 10.0f };
	float radius = 1.0f;

protected:
	void Init() override;
	void Inspector() override;

private:
	friend class PhysicsSystem;
	void UpdatePosition();
};

}
