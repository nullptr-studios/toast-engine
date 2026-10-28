/**
 * @file fog_volume.cpp
 * @author dario
 * @date 26/10/2026
 */

#include "fog_volume.hpp"

#include <toast/renderer/vulkan_renderer.hpp>

namespace toast {

void FogVolume::init() {
	renderer::registerFogVolumeProxy(this);
	m_registered_proxy = true;
}

void FogVolume::end() {
	if (!m_registered_proxy) {
		return;
	}

	renderer::unregisterFogVolumeProxy(this);
	m_registered_proxy = false;
}

void FogVolume::destroy() {
	end();
}

}
