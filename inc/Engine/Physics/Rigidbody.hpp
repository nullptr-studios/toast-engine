/// @file Rigidbody.hpp
/// @author Xein
/// @date 22 Dec 2025

#pragma once
#include "Engine/Physics/Line.hpp"

#include <Engine/Toast/Components/Component.hpp>
#include <glm/glm.hpp>

namespace physics {

class Rigidbody : public toast::Component {
public:
	REGISTER_TYPE(Rigidbody);
	glm::vec2 velocity = { 0.0f, 0.0f };
	float radius = 1.0f;
	bool simulate = true;

	auto data() const -> RigidbodyData;
	void data(const RigidbodyData& data);

protected:
	void Init() override;
	void Inspector() override;
	void Destroy() override;

private:
	friend class PhysicsSystem;
};

}
