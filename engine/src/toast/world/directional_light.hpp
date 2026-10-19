/**
 * @file directional_light.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "light.hpp"

#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Icon("DirectionalLight")]] TOAST_API DirectionalLight : public Light {
public:
	DirectionalLight() { setLightType(LightType::directional); }

	~DirectionalLight() override = default;

	[[nodiscard]]
	auto angularDiameter() const -> float {
		return m_angular_diameter;
	}

private:
	/// Marched voxel shadow penumbra width where the sun is about 0.53
	[[Reflect, Name("Angular Diameter"), Unit("deg"), Range(0.0, 10.0), Group("Area")]]
	float m_angular_diameter = 0.53f;
};
}
