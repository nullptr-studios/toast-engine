/// @file PostProcessVolume.cpp
/// @author dario
/// @date 06/04/2026.

#include "Toast/Renderer/PostProcessVolume.hpp"

#include "Toast/Renderer/Camera.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/PostProcessManager.hpp"
#include "Toast/Renderer/PostProcessing/PostProcessFactory.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

void PostProcessVolume::Init() {
	Actor::Init();
	RebuildRuntimeStack();
}

void PostProcessVolume::Tick() {
	ApplyOverrideIfInside();
}

void PostProcessVolume::ApplyOverrideIfInside() {
	if (!m_enabledVolume) {
		return;
	}

	auto* rendererBase = renderer::IRendererBase::GetInstance();
	auto* ppm = rendererBase ? rendererBase->GetPostProcessManager() : nullptr;
	if (!ppm) {
		return;
	}

	if (!IsCameraInside2D()) {
		return;
	}

	if (m_runtimeStack.size() != m_stack.size()) {
		RebuildRuntimeStack();
	}

	ppm->SetOverrideStack(&m_runtimeStack, m_priority);
}

json_t PostProcessVolume::Save() const {
	json_t j = Actor::Save();
	j["priority"] = m_priority;
	j["halfExtents"] = m_halfExtents;
	j["enabledVolume"] = m_enabledVolume;

	auto* rendererBase = renderer::IRendererBase::GetInstance();
	auto* ppm = rendererBase ? rendererBase->GetPostProcessManager() : nullptr;
	if (ppm) {
		j["volumeStack"] = ppm->SaveStack(m_stack);
	} else {
		json_t stackJson = json_t::array();
		for (const auto& pass : m_stack) {
			if (pass) {
				stackJson.push_back(pass->SaveState());
			}
		}
		j["volumeStack"] = stackJson;
	}

	return j;
}

void PostProcessVolume::Load(json_t j, bool force_create) {
	Actor::Load(j, force_create);
	m_priority = j.value("priority", m_priority);
	m_halfExtents = j.value("halfExtents", m_halfExtents);
	m_enabledVolume = j.value("enabledVolume", m_enabledVolume);

	if (j.contains("volumeStack")) {
		auto* rendererBase = renderer::IRendererBase::GetInstance();
		auto* ppm = rendererBase ? rendererBase->GetPostProcessManager() : nullptr;
		if (ppm) {
			(void)ppm->LoadStackInto(j.at("volumeStack"), m_stack);
		} else if (j.at("volumeStack").is_array()) {
			m_stack.clear();
			for (const auto& item : j.at("volumeStack")) {
				if (!item.is_object() || !item.contains("type")) {
					continue;
				}
				auto pass = renderer::post::CreateByType(item.at("type").get<std::string>());
				if (!pass) {
					continue;
				}
				pass->LoadState(item);
				m_stack.push_back(std::move(pass));
			}
		}
	}

	RebuildRuntimeStack();
}

void PostProcessVolume::RebuildRuntimeStack() {
	m_runtimeStack.clear();
	m_runtimeStack.reserve(m_stack.size());
	for (const auto& pass : m_stack) {
		m_runtimeStack.push_back(pass.get());
	}
}

bool PostProcessVolume::IsCameraInside2D() const {
	auto* rendererBase = renderer::IRendererBase::GetInstance();
	auto* activeCamera = rendererBase ? rendererBase->GetActiveCamera() : nullptr;
	if (!activeCamera) {
		return false;
	}

	const glm::vec3 volumePos = const_cast<PostProcessVolume*>(this)->transform()->worldPosition();
	const glm::vec3 camPos = activeCamera->transform()->worldPosition();

	return glm::abs(camPos.x - volumePos.x) <= m_halfExtents.x && glm::abs(camPos.y - volumePos.y) <= m_halfExtents.y;
}

#ifdef TOAST_EDITOR
void PostProcessVolume::DrawDebugBounds() const {
	if (!m_enabledVolume) {
		return;
	}

	const glm::vec3 center = const_cast<PostProcessVolume*>(this)->transform()->worldPosition();
	const glm::vec3 a(center.x - m_halfExtents.x, center.y - m_halfExtents.y, center.z);
	const glm::vec3 b(center.x + m_halfExtents.x, center.y - m_halfExtents.y, center.z);
	const glm::vec3 c(center.x + m_halfExtents.x, center.y + m_halfExtents.y, center.z);
	const glm::vec3 d(center.x - m_halfExtents.x, center.y + m_halfExtents.y, center.z);

	const glm::vec4 color = IsCameraInside2D() ? glm::vec4(1.0f, 0.45f, 0.15f, 1.0f) : glm::vec4(0.25f, 0.95f, 0.95f, 1.0f);
	renderer::DebugLine(a, b, color);
	renderer::DebugLine(b, c, color);
	renderer::DebugLine(c, d, color);
	renderer::DebugLine(d, a, color);
}

void PostProcessVolume::EditorTick() {
	ApplyOverrideIfInside();
	DrawDebugBounds();
}

void PostProcessVolume::Inspector() {
	Actor::Inspector();

	ImGui::SeparatorText("Post Process Volume");
	ImGui::Checkbox("Enabled Volume", &m_enabledVolume);
	ImGui::DragInt("Priority", &m_priority, 1, -1000, 1000);
	ImGui::DragFloat2("Half Extents", &m_halfExtents.x, 0.1f, 0.1f, 10000.0f);

	auto* rendererBase = renderer::IRendererBase::GetInstance();
	auto* ppm = rendererBase ? rendererBase->GetPostProcessManager() : nullptr;
	if (!ppm) {
		ImGui::TextUnformatted("PostProcessManager unavailable.");
		return;
	}

	if (ImGui::Button("Load From Global Post Process")) {
		bool loaded = ppm->LoadGlobalFromConfigInto(m_stack);
		if (!loaded) {
			// Fallback: clone current runtime global stack if saved config is missing.
			loaded = ppm->LoadStackInto(ppm->SaveGlobalStack(), m_stack);
		}
		if (loaded) {
			RebuildRuntimeStack();
		}
	}

	static int selectedType = 0;
	const auto& types = renderer::post::AvailableTypes();
	if (!types.empty()) {
		if (selectedType >= static_cast<int>(types.size())) {
			selectedType = 0;
		}
		ImGui::SetNextItemWidth(220.f);
		if (ImGui::BeginCombo("Add Volume Pass", types[selectedType].c_str())) {
			for (int i = 0; i < static_cast<int>(types.size()); ++i) {
				const bool isSelected = i == selectedType;
				if (ImGui::Selectable(types[i].c_str(), isSelected)) {
					selectedType = i;
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button("Add Pass") && selectedType < static_cast<int>(types.size())) {
			auto pass = renderer::post::CreateByType(types[selectedType]);
			if (pass) {
				m_stack.push_back(std::move(pass));
				RebuildRuntimeStack();
			}
		}
	}

	for (size_t i = 0; i < m_stack.size(); ++i) {
		auto& pass = m_stack[i];
		if (!pass) {
			continue;
		}

		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(pass->GetTypeId().data())) {
			bool enabled = pass->IsEnabled();
			if (ImGui::Checkbox("Enabled", &enabled)) {
				pass->SetEnabled(enabled);
			}
			float blend = pass->GetBlend();
			if (ImGui::SliderFloat("Blend", &blend, 0.f, 1.f)) {
				pass->SetBlend(blend);
			}

			if (ImGui::SmallButton("Up") && i > 0) {
				std::swap(m_stack[i], m_stack[i - 1]);
				RebuildRuntimeStack();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Down") && i + 1 < m_stack.size()) {
				std::swap(m_stack[i], m_stack[i + 1]);
				RebuildRuntimeStack();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove")) {
				m_stack.erase(m_stack.begin() + static_cast<std::ptrdiff_t>(i));
				RebuildRuntimeStack();
				ImGui::TreePop();
				ImGui::PopID();
				break;
			}

			pass->Inspector();
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}
#endif

