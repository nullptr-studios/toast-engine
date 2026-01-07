//
// Created by dario on 17/09/2025.
//

#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "glm/ext/matrix_transform.hpp"

#include <Toast/Renderer/Camera.hpp>

void toast::Camera::Init() {
	Actor::Init();
}

void toast::Camera::Begin() {
	Actor::Begin();
	if (m_isActiveCamera) {
		renderer::IRendererBase::GetInstance()->SetActiveCamera(this);
	}
}

void toast::Camera::Destroy() {
	Actor::Destroy();

	if (renderer::IRendererBase::GetInstance()->GetActiveCamera() == this) {
		renderer::IRendererBase::GetInstance()->SetActiveCamera(nullptr);
	}
}

json_t toast::Camera::Save() const {
	json_t j = Actor::Save();
	j["isActiveCamera"] = m_isActiveCamera;
	return j;
}

void toast::Camera::Load(json_t j, bool force_create) {
	Actor::Load(j);
	m_isActiveCamera = j.value("isActiveCamera", false);
}

glm::mat4 toast::Camera::GetViewMatrix() const {
	PROFILE_ZONE;
	glm::quat q = glm::quat(transform()->rotationRadians());    // from Euler (pitch, yaw, roll)
	glm::mat4 rot = glm::mat4_cast(glm::conjugate(q));
	glm::mat4 trans = glm::translate(glm::mat4(1.0f), -transform()->position());
	glm::mat4 m = rot * trans;
	return m;
}

void toast::Camera::SetActiveCamera(bool active) {
	m_isActiveCamera = active;
	if (m_isActiveCamera) {
		renderer::IRendererBase::GetInstance()->SetActiveCamera(this);
	} else {
		renderer::IRendererBase::GetInstance()->SetActiveCamera(nullptr);
	}
}
