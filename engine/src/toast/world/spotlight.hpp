/**
 * @file spotlight.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "light.hpp"

#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Icon("SpotLight")]] TOAST_API Spotlight : public Light {
public:
	Spotlight() = default;

	~Spotlight() = default;

private:
	[[Reflect, Unit("°"), Range(0.0, 90.0)]]
	float m_inner_radius = 15.0f;

	[[Reflect, Unit("°"), Range(0.0, 90.0)]]
	float m_outer_radius = 25.0f;

	[[Reflect, Unit("m")]]
	float m_attenuation = 10.0f;
};
}
