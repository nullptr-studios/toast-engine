#include "reflection_probe.hpp"

#include <toast/renderer/vulkan_renderer.hpp>

namespace toast {

void ReflectionProbe::init() {
	renderer::registerReflectionProbeProxy(this);
	m_registered_proxy = true;
}

void ReflectionProbe::end() {
	if (!m_registered_proxy) {
		return;
	}

	renderer::unregisterReflectionProbeProxy(this);
	m_registered_proxy = false;
}

void ReflectionProbe::destroy() {
	end();
}

}
