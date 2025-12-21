/// @file GlobalLight.hpp
/// @author dario
/// @date 06/12/2025.

#pragma once

#include "Engine/Toast/Objects/Actor.hpp"
#include "glm/vec3.hpp"

class GlobalLight : public toast::Actor {
public:
	REGISTER_TYPE(GlobalLight);

	void Init() override;

	void Load(json_t j, bool force_create) override;
	json_t Save() const override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

private:
	float m_intensity = 1.0f;
	glm::vec3 m_color = glm::vec3(1.0f);
	bool m_enableLight = true;
};
