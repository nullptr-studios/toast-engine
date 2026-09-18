/**
 * @file point_light.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "light.hpp"

#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Icon("PointLight")]] TOAST_API PointLight : public Light {
public:
	PointLight() { setLightType(LightType::point); }

	~PointLight() override = default;

	[[nodiscard]]
	auto attenuation() const -> float {
		return m_attenuation;
	}

private:
	[[Reflect, Unit("m"), Range(0, 1000)]]
	float m_attenuation = 10.0f;
};
}
