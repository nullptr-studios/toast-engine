#include "mesh_node.hpp"

#include <toast/renderer/vulkan_renderer.hpp>

namespace toast {

void MeshNode::updateInspectorMessages() {
	static const NodeMessage message {
	  .severity = NodeMessage::warning,
	  .id = 5,
	  .text = "MeshNode requires a Mesh to render",
	};

	if (m_mesh.hasValue()) {
		removeInspectorMessage(message);
	} else {
		addInspectorMessage(message);
	}
}

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
