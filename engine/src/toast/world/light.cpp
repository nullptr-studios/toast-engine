#include "light.hpp"

#include <toast/renderer/vulkan_renderer.hpp>

namespace toast {

void Light::init() {
	renderer::registerLightNodeProxy(this);
	m_registered_proxy = true;
}

void Light::end() {
	if (!m_registered_proxy) {
		return;
	}

	renderer::unregisterLightNodeProxy(this);
	m_registered_proxy = false;
}

void Light::destroy() {
	end();
}

}
