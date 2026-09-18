#include "irradiance_volume.hpp"

#include <toast/renderer/vulkan_renderer.hpp>

namespace toast {

void IrradianceVolume::init() {
	renderer::registerIrradianceVolumeProxy(this);
	m_registered_proxy = true;
}

void IrradianceVolume::end() {
	if (!m_registered_proxy) {
		return;
	}

	renderer::unregisterIrradianceVolumeProxy(this);
	m_registered_proxy = false;
}

void IrradianceVolume::destroy() {
	end();
}

}
