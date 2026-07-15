#include "mesh_node.hpp"

#include "toast/renderer/vulkan_renderer.hpp"

namespace toast {

void MeshNode::init() {
	renderer::registerMeshNodeProxy(this);
	m_registered_proxy = true;
}

void MeshNode::end() {
	if (!m_registered_proxy) {
		return;
	}

	renderer::unregisterMeshNodeProxy(this);
	m_registered_proxy = false;
}

void MeshNode::destroy() {
	end();
}

}
