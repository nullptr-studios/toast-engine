/// @file PostProcessManager.cpp
/// @author dario
/// @date 23/03/2026.

#include "Toast/Renderer/PostProcessManager.hpp"

#include "Toast/Log.hpp"
#include "Toast/Renderer/OpenGL/GLStateCache.hpp"
#include "Toast/Renderer/PostProcessing/PostProcessFactory.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <algorithm>
#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

PostProcessManager* PostProcessManager::instance = nullptr;

GLuint PostProcessManager::ExecuteStack(std::vector<IPostProcess*>* stack, Framebuffer* inputFBO) {
	Framebuffer* srcFBO = inputFBO;
	Framebuffer* dstFBO = m_fboA;
	bool tonemappingExecuted = false;

	for (auto* effect : *stack) {
		if (!effect || !effect->IsEnabled()) {
			continue;
		}
		if (effect->GetTypeId() == "Tonemaping") {
			tonemappingExecuted = true;
		}
		effect->Execute(srcFBO, dstFBO);

		srcFBO = dstFBO;
		dstFBO = (dstFBO == m_fboA) ? m_fboB : m_fboA;
	}

	// Tonemapping is mandatory for HDR->SDR correctness.
	if (!tonemappingExecuted && m_mandatoryTonemapping) {
		m_mandatoryTonemapping->Execute(srcFBO, dstFBO);
		srcFBO = dstFBO;
	}

	return srcFBO->GetColorTexture(0);
}

PostProcessManager::PostProcessManager() {
	if (instance) {
		TOAST_ERROR("Multiple instances of PostProcessManager, This should never happen.");
	}
	instance = this;
	m_mandatoryTonemapping = renderer::post::CreateByType("Tonemaping");
}

PostProcessManager::~PostProcessManager() {
	delete m_fboA;
	delete m_fboB;
}

void PostProcessManager::PostProcessPass(Framebuffer* inputFBO, Framebuffer* outputFBO) {
	if (!m_fboA || !m_fboB) {
		TOAST_WARN("PostProcessManager buffers not initialized, falling back to direct blit");
		inputFBO->BlitTo(outputFBO, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		ClearOverride();
		return;
	}

	GLuint finalTex = 0;
	if (m_overrideStack) {
		finalTex = ExecuteStack(m_overrideStack, inputFBO);
	} else {
		std::vector<IPostProcess*> temp;
		temp.reserve(m_globalStack.size());
		for (auto& e : m_globalStack) {
			temp.push_back(e.get());
		}
		finalTex = ExecuteStack(&temp, inputFBO);
	}

	// blit the final texture into output FB
	Framebuffer* srcFBO = nullptr;

	if (finalTex == inputFBO->GetColorTexture(0)) {
		srcFBO = inputFBO;
	} else if (finalTex == m_texA) {
		srcFBO = m_fboA;
	} else if (finalTex == m_texB) {
		srcFBO = m_fboB;
	} else {
		TOAST_WARN("PostProcessManager: final texture not recognized, falling back to inputFBO blit");
		inputFBO->BlitTo(outputFBO, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		ClearOverride();
		return;
	}

	renderer::SetDepthTest(false);

	outputFBO->bind();

	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	glViewport(0, 0, outputFBO->Width(), outputFBO->Height());
	glScissor(0, 0, outputFBO->Width(), outputFBO->Height());

	// glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	srcFBO->BlitTo(outputFBO, GL_COLOR_BUFFER_BIT, GL_LINEAR);

	renderer::SetDepthTest(true);

	Framebuffer::unbind();
	ClearOverride();
}

void PostProcessManager::AddGlobalProcess(std::unique_ptr<IPostProcess> effect) {
	if (!effect) {
		return;
	}
	m_globalStack.push_back(std::move(effect));
}

bool PostProcessManager::RemoveGlobalProcess(size_t index) {
	if (index >= m_globalStack.size()) {
		return false;
	}
	m_globalStack.erase(m_globalStack.begin() + static_cast<std::ptrdiff_t>(index));
	return true;
}

bool PostProcessManager::MoveGlobalProcess(size_t from, size_t to) {
	if (from >= m_globalStack.size() || to >= m_globalStack.size() || from == to) {
		return false;
	}
	std::swap(m_globalStack[from], m_globalStack[to]);
	return true;
}

bool PostProcessManager::AddGlobalProcessByType(const std::string& typeId) {
	auto pass = renderer::post::CreateByType(typeId);
	if (!pass) {
		TOAST_WARN("PostProcessManager: unknown post process type '{}'", typeId);
		return false;
	}
	AddGlobalProcess(std::move(pass));
	return true;
}

void PostProcessManager::ClearGlobalProcesses() {
	m_globalStack.clear();
}

void PostProcessManager::SetOverrideStack(std::vector<IPostProcess*>* stack, int priority) {
	if (priority >= m_overridePriority) {
		m_overrideStack = stack;
		m_overridePriority = priority;
	}
}

void PostProcessManager::ClearOverride() {
	m_overrideStack = nullptr;
	m_overridePriority = -1;
}

json_t PostProcessManager::SaveStack(const std::vector<IPostProcess*>& stack) const {
	json_t arr = json_t::array();
	for (auto* effect : stack) {
		if (!effect) {
			continue;
		}
		arr.push_back(effect->SaveState());
	}
	return arr;
}

json_t PostProcessManager::SaveStack(const std::vector<std::unique_ptr<IPostProcess>>& stack) const {
	json_t arr = json_t::array();
	for (const auto& effect : stack) {
		if (!effect) {
			continue;
		}
		arr.push_back(effect->SaveState());
	}
	return arr;
}

json_t PostProcessManager::SaveGlobalStack() const {
	json_t arr = json_t::array();
	for (const auto& effect : m_globalStack) {
		if (!effect) {
			continue;
		}
		arr.push_back(effect->SaveState());
	}
	return arr;
}

bool PostProcessManager::LoadStackInto(const json_t& j, std::vector<std::unique_ptr<IPostProcess>>& dst) {
	if (!j.is_array()) {
		return false;
	}

	std::vector<std::unique_ptr<IPostProcess>> parsed;
	parsed.reserve(j.size());

	for (const auto& item : j) {
		if (!item.is_object() || !item.contains("type")) {
			continue;
		}

		const auto type = item.at("type").get<std::string>();
		auto pass = renderer::post::CreateByType(type);
		if (!pass) {
			TOAST_WARN("PostProcessManager: could not create pass '{}' while loading stack", type);
			continue;
		}
		pass->LoadState(item);
		parsed.push_back(std::move(pass));
	}

	dst = std::move(parsed);
	return true;
}

bool PostProcessManager::LoadGlobalStack(const json_t& j) {
	return LoadStackInto(j, m_globalStack);
}

bool PostProcessManager::LoadGlobalFromConfigInto(std::vector<std::unique_ptr<IPostProcess>>& dst, const std::string& path) {
	std::string content;
	if (!resource::ResourceManager::LoadConfig(path, content)) {
		return false;
	}

	try {
		auto root = json_t::parse(content);
		if (!root.contains("globalStack")) {
			return false;
		}
		return LoadStackInto(root.at("globalStack"), dst);
	} catch (const std::exception& e) {
		TOAST_WARN("PostProcessManager: failed to parse '{}': {}", path, e.what());
		return false;
	}
}

bool PostProcessManager::SaveGlobalToConfig(const std::string& path) const {
	json_t root {};
	root["version"] = 1;
	root["globalStack"] = SaveGlobalStack();
	return resource::ResourceManager::SaveConfig(path, root.dump(1));
}

bool PostProcessManager::LoadGlobalFromConfig(const std::string& path) {
	std::string content;
	if (!resource::ResourceManager::LoadConfig(path, content)) {
		return false;
	}

	try {
		auto root = json_t::parse(content);
		if (!root.contains("globalStack")) {
			return false;
		}
		return LoadGlobalStack(root.at("globalStack"));
	} catch (const std::exception& e) {
		TOAST_WARN("PostProcessManager: failed to parse '{}': {}", path, e.what());
		return false;
	}
}

void PostProcessManager::GlobalInspector() {
#ifdef TOAST_EDITOR
	if (ImGui::CollapsingHeader("Global Post-Processing Effects")) {
		if (ImGui::Button("Save Global Stack")) {
			(void)SaveGlobalToConfig();
		}
		ImGui::SameLine();
		if (ImGui::Button("Load Global Stack")) {
			(void)LoadGlobalFromConfig();
		}

		static int selectedType = 0;
		const auto& types = renderer::post::AvailableTypes();
		if (!types.empty()) {
			if (selectedType >= static_cast<int>(types.size())) {
				selectedType = 0;
			}
			ImGui::SetNextItemWidth(220.f);
			if (ImGui::BeginCombo("Add Pass", types[selectedType].c_str())) {
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
			if (ImGui::Button("Add") && selectedType < static_cast<int>(types.size())) {
				AddGlobalProcessByType(types[selectedType]);
			}
		}

		for (size_t i = 0; i < m_globalStack.size(); ++i) {
			auto& effect = m_globalStack[i];
			if (!effect) {
				continue;
			}

			ImGui::PushID(static_cast<int>(i));
			if (ImGui::TreeNode(effect->GetTypeId().data())) {
				bool enabled = effect->IsEnabled();
				if (ImGui::Checkbox("Enabled", &enabled)) {
					effect->SetEnabled(enabled);
				}
				float blend = effect->GetBlend();
				if (ImGui::SliderFloat("Blend", &blend, 0.f, 1.f)) {
					effect->SetBlend(blend);
				}

				if (ImGui::SmallButton("Up") && i > 0) {
					MoveGlobalProcess(i, i - 1);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Down") && i + 1 < m_globalStack.size()) {
					MoveGlobalProcess(i, i + 1);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove")) {
					RemoveGlobalProcess(i);
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}

				effect->Inspector();
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}
#endif
}

void PostProcessManager::InitBuffers(const glm::ivec2& resolution) {
	Framebuffer::Specs specs = { resolution.x, resolution.y };
	if (!m_fboA) {
		m_fboA = new Framebuffer(specs);
		m_fboA->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		m_fboA->Build();
	} else {
		m_fboA->Resize(resolution.x, resolution.y);
	}

	if (!m_fboB) {
		m_fboB = new Framebuffer(specs);
		m_fboB->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		m_fboB->Build();
	} else {
		m_fboB->Resize(resolution.x, resolution.y);
	}

	m_texA = m_fboA->GetColorTexture(0);
	m_texB = m_fboB->GetColorTexture(0);
}
