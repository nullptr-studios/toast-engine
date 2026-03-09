#include "Toast/Nodes/Node.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include "Toast/SubNodes/TransformSubNode.hpp"
#include "Toast/Event/ListenerSubNode.hpp"
#include "Toast/GlmJson.hpp"
#include "Toast/Nodes/Node3D.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"

namespace toast {

Node3D::Node3D() {
	// Adds a transform to the Node3D
	m_transform = std::make_unique<TransformSubNode>();
	m_transform->m_id = Factory::AssignId();
	m_transform->SetAttachedNode3D(this);
	m_transform->m_parent = this;
	// Adds an Event Listener component
	m_listener = std::make_unique<event::ListenerSubNode>();
}

json_t Node3D::Save() const {
	PROFILE_ZONE_C(0x00FF00);    // Green for serialization
	json_t j = Node::Save();
	json_t transform_j;
	transform_j["position"] = m_transform->position();
	transform_j["rotation"] = m_transform->rotationQuat();
	transform_j["scale"] = m_transform->scale();
	j["transform"] = transform_j;
	return j;
}

void Node3D::Load(json_t j, bool force_create) {
	PROFILE_ZONE_C(0x00FFFF);    // Cyan for deserialization

	auto transform_j = j["transform"];
	m_transform->position(transform_j["position"].get<glm::vec3>());
	m_transform->rotationQuat(transform_j["rotation"].get<glm::quat>());
	m_transform->scale(transform_j["scale"].get<glm::vec3>());

	Node::Load(j, force_create);
}

#ifdef TOAST_EDITOR
void Node3D::Inspector() {
	Node::Inspector();
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(20);
		transform()->Inspector();
		ImGui::Unindent(20);
	}
}
#endif

void Node3D::Init() { }

TransformSubNode* Node3D::transform() const {
	if (!m_transform) {
		throw ToastException("No transform provided in actor???");
	}
	return m_transform.get();
}

event::ListenerSubNode* Node3D::listener() const {
	if (!m_listener) {
		throw ToastException("No listener provided in actor???");
	}
	return m_listener.get();
}

}
