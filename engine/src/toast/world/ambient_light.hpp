/**
 * @file ambient_light.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "light.hpp"

#include <toast/assets/environment_map.hpp>
#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Icon("Environment")]] TOAST_API AmbientLight : public Light {
public:
	AmbientLight() { setLightType(LightType::ambient); }

	~AmbientLight() override = default;

	/// @returns the HDRI that replaces the global environment map, data() 0 to keep the global one
	[[nodiscard]]
	auto environmentMapUid() const noexcept -> UID {
		return m_environment_map.uid();
	}

private:
	/// Replaces the Renderer Settings environment map while this light is enabled
	[[Reflect, Name("Environment Map")]]
	assets::Handle<assets::EnvironmentMap> m_environment_map;
};
}
