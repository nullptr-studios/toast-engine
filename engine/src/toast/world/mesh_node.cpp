#include "mesh_node.hpp"

#include "toast/engine.hpp"

namespace toast {

void MeshNode::init() {
	Engine::get()->registerMeshNodeProxy(this);
	m_registered_proxy = true;
}

void MeshNode::end() {
	if (!m_registered_proxy) {
		return;
	}

	Engine::get()->unregisterMeshNodeProxy(this);
	m_registered_proxy = false;
}

void MeshNode::destroy() {
	end();
}

}
