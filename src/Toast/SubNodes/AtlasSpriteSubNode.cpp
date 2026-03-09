/// @file AtlasSpriteSubNode.cpp
/// @author dario
/// @date 04/02/2026.

#include "Toast/SubNodes/AtlasSpriteSubNode.hpp"

#include "Toast/SubNodes/AtlasRendererSubNode.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

namespace toast {

void AtlasSpriteSubNode::Init() {
	TransformSubNode::Init();

	SetRunEarlyTick(false);
	SetRunTick(false);
	SetRunLateTick(false);

#ifdef TOAST_EDITOR
	// Autoadd to parent
	// HACK: this is not really the best solution and the engine should know when are we in play mode or paused
	dynamic_cast<AtlasRendererSubNode*>(parent())->AddSpriteToCache(this);
#endif
}

void AtlasSpriteSubNode::Destroy() {
	toast::Node* p = parent();
	while (p) {
		if (auto* renderer = dynamic_cast<AtlasRendererSubNode*>(p)) {
			renderer->RemoveSpriteFromCache(this);
			break;
		}
		p = p->parent();
	}
}

void AtlasSpriteSubNode::Load(json_t j, bool force_create) {
	TransformSubNode::Load(j, force_create);

	if (j.contains("regionName")) {
		m.regionName = j.at("regionName").get<std::string>();
	}

	if (j.contains("color")) {
		auto& color_arr = j.at("color");
		if (color_arr.is_array() && color_arr.size() >= 4) {
			m.color.r = color_arr[0].get<float>();
			m.color.g = color_arr[1].get<float>();
			m.color.b = color_arr[2].get<float>();
			m.color.a = color_arr[3].get<float>();
		}
	}
}

json_t AtlasSpriteSubNode::Save() const {
	json_t j = TransformSubNode::Save();
	j["regionName"] = m.regionName;
	j["color"] = { m.color.r, m.color.g, m.color.b, m.color.a };
	return j;
}

#ifdef TOAST_EDITOR

void AtlasSpriteSubNode::Inspector() {
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(20);
		TransformSubNode::Inspector();
		ImGui::Unindent(20);
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Atlas Sprite");

	ImGui::Text("Region: %s", m.regionName.empty() ? "<none>" : m.regionName.c_str());

	// Color picker
	float color[4] = { m.color.r, m.color.g, m.color.b, m.color.a };    // NOLINT
	if (ImGui::ColorEdit4("Color", color)) {
		m.color.r = color[0];
		m.color.g = color[1];
		m.color.b = color[2];
		m.color.a = color[3];
	}
}

#endif

}
