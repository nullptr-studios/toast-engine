#include "mesh_node.hpp"

#include "skeletal_node.hpp"

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

auto MeshNode::inspectorOptions(std::string_view field_name) -> std::vector<std::string> {
	std::vector<std::string> options;
	if (field_name != "m_skin_name") {
		return options;
	}
	for (Box<Node> cursor = parent(); cursor.exists(); cursor = cursor->parent()) {
		if (auto skeletal = cursor.as<SkeletalNode>(); skeletal.exists()) {
			if (const assets::Skeleton* skeleton = skeletal->skeleton(); skeleton != nullptr) {
				for (const auto& skin : skeleton->skins()) {
					options.push_back(skin.name);
				}
			}
			break;
		}
	}
	return options;
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
