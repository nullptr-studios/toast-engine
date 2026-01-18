///@file Camera.hpp
///@author Dario
///@date 17/09/25

#pragma once
#include "Toast/Objects/Actor.hpp"

#include <glm/glm.hpp>

namespace toast {
class Camera : public toast::Actor {
public:
	REGISTER_TYPE(Camera);

	void Init() override;
	void Begin() override;
	void Destroy() override;

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

	[[nodiscard]]
	glm::mat4 GetViewMatrix() const;

	[[nodiscard]]
	bool IsActiveCamera() const {
		return m_isActiveCamera;
	}

	void SetActiveCamera(bool active);

private:
	bool m_isActiveCamera = false;

	alignas(16) glm::mat4 m_viewMatrix = glm::mat4(1.0f);
};
}
