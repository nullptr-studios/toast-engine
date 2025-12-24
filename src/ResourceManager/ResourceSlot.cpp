/// @file ResourceSlot.cpp
/// @author dario
/// @date 27/10/2025.

#include "Engine/Core/Log.hpp"
#include "Engine/Resources/Mesh.hpp"
#include "Engine/Resources/ResourceManager.hpp"
#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include "Engine/Renderer/Material.hpp"
#include "Engine/Resources/Spine/SpineAtlas.hpp"

#include <Engine/Resources/ResourceSlot.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

struct ResourceHandler {
	std::vector<std::string> extensions;
	std::function<std::shared_ptr<IResource>(const std::string&)> loader;
};

static std::unordered_map<resource::ResourceType, ResourceHandler> s_resourceRegistry;

static void EnsureRegistryInitialized() {
	if (!s_resourceRegistry.empty()) {
		return;
	}

	s_resourceRegistry[resource::ResourceType::TEXTURE] = ResourceHandler {
		{ ".png", ".jpg", ".bmp", ".tga" },
		[](const std::string& path) -> std::shared_ptr<IResource> {
		  auto* mgr = resource::ResourceManager::GetInstance();
		  if (!mgr) {
			  return nullptr;
		  }
		  return mgr->LoadResource<Texture>(path);
		 }
	};

	s_resourceRegistry[resource::ResourceType::MODEL] = ResourceHandler { { ".obj" }, [](const std::string& path) -> std::shared_ptr<IResource> {
		                                                                     auto* mgr = resource::ResourceManager::GetInstance();
		                                                                     if (!mgr) {
			                                                                     return nullptr;
		                                                                     }
		                                                                     return mgr->LoadResource<renderer::Mesh>(path);
		                                                                   } };

	s_resourceRegistry[resource::ResourceType::SHADER] = ResourceHandler { { ".shader" }, [](const std::string& path) -> std::shared_ptr<IResource> {
		                                                                      auto* mgr = resource::ResourceManager::GetInstance();
		                                                                      if (!mgr) {
			                                                                      return nullptr;
		                                                                      }
		                                                                      return mgr->LoadResource<renderer::Shader>(path);
		                                                                    } };

	s_resourceRegistry[resource::ResourceType::AUDIO] = ResourceHandler {
		{ ".wav", ".mp3", ".ogg" },
    nullptr
	};

	s_resourceRegistry[resource::ResourceType::FONT] = ResourceHandler {
		{ ".ttf", ".otf" },
    nullptr
	};

	s_resourceRegistry[resource::ResourceType::MATERIAL] = ResourceHandler { { ".mat" }, [](const std::string& path) -> std::shared_ptr<IResource> {
		                                                                        auto* mgr = resource::ResourceManager::GetInstance();
		                                                                        if (!mgr) {
			                                                                        return nullptr;
		                                                                        }
		                                                                        return mgr->LoadResource<renderer::Material>(path);
		                                                                      } };

	s_resourceRegistry[resource::ResourceType::SPINE_ATLAS] =
	    ResourceHandler { { ".atlas" }, [](const std::string& path) -> std::shared_ptr<IResource> {
		                     auto* mgr = resource::ResourceManager::GetInstance();
		                     if (!mgr) {
			                     return nullptr;
		                     }
		                     return mgr->LoadResource<SpineAtlas>(path);
		                   } };

	s_resourceRegistry[resource::ResourceType::SPINE_SKELETON_DATA] = ResourceHandler {
		{ ".json", ".skel" },
    nullptr
	};
}

static std::string ToLower(std::string s) {
	std::ranges::transform(s, s.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return s;
}

auto type_to_string = [](resource::ResourceType t) {
	switch (t) {
		case resource::ResourceType::TEXTURE: return "Texture";
		case resource::ResourceType::MODEL: return "Model";
		case resource::ResourceType::AUDIO: return "Audio";
		case resource::ResourceType::SHADER: return "Shader";
		case resource::ResourceType::FONT: return "Font";
		case resource::ResourceType::SPINE_ATLAS: return "SpineAtlas";
		case resource::ResourceType::SPINE_SKELETON_DATA: return "SpineSkeleton";
		case resource::ResourceType::MATERIAL: return "Material";
		default: return "Unknown Resource";
	}
};

editor::ResourceSlot::ResourceSlot(resource::ResourceType required_type, std::string default_path)
    : m_defaultPath(std::move(default_path)),
      m_requiredType(required_type) {
#ifdef TOAST_EDITOR
	EnsureRegistryInitialized();
#else
	/// NO SAFETY CHECKS IF THE PATH IS WRONG ITS YOUR FAULT
	m_selectedEntry.relativePath = m_defaultPath;
#endif
}

#ifdef TOAST_EDITOR

void editor::ResourceSlot::ProcessDrop(Entry* e) {
	if (!e) {
		return;
	}

	if (!CheckCorrectType(e)) {
		// Show a modal popup to the user explaining the bad drop
		m_typeErrorMessage = "Invalid resource type: " + e->name + "\nExpected: ";
		// append expected type name
		switch (m_requiredType) {
			case resource::ResourceType::TEXTURE: m_typeErrorMessage += "Texture"; break;
			case resource::ResourceType::MODEL: m_typeErrorMessage += "Model"; break;
			case resource::ResourceType::AUDIO: m_typeErrorMessage += "Audio"; break;
			case resource::ResourceType::SHADER: m_typeErrorMessage += "Shader"; break;
			case resource::ResourceType::FONT: m_typeErrorMessage += "Font"; break;
			default: m_typeErrorMessage += "Unknown"; break;
		}

		// Append allowed extensions if available
		auto it = s_resourceRegistry.find(m_requiredType);
		if (it != s_resourceRegistry.end()) {
			const auto& exts = it->second.extensions;
			if (!exts.empty()) {
				m_typeErrorMessage += "\nAllowed extensions: ";
				for (size_t i = 0; i < exts.size(); ++i) {
					m_typeErrorMessage += exts[i];
					if (i + 1 < exts.size()) {
						m_typeErrorMessage += ", ";
					}
				}
			}
		}

		m_showTypeErrorPopup = true;

		TOAST_WARN("ResourceSlot: Dropped resource has invalid extension: {}", e->name);
		return;
	}
	m_selectedEntry = *e;

	// Always notify listeners with the path, regardless of loading success
	// (we care about the path, not whether it can be loaded)
	if (m_onDropped) {
		m_onDropped(ToForwardSlashes(e->relativePath.string()));
	}

	std::shared_ptr<IResource> resource;
	auto it = s_resourceRegistry.find(m_requiredType);
	if (it != s_resourceRegistry.end() && it->second.loader) {
		resource = it->second.loader(ToForwardSlashes(e->relativePath.string()));
	} else {
		TOAST_WARN("ResourceSlot: No loader registered for resource type {}", static_cast<int>(m_requiredType));
	}

	if (resource) {
		// m_resource = resource;
		TOAST_INFO("ResourceSlot: Bound resource: {}", e->name);
	} else {
		TOAST_WARN("ResourceSlot: Failed to load resource, but path stored: {}", e->name);
	}
}

void editor::ResourceSlot::RenderThumbnailArea() {
	const ImVec2 thumbnail_size(64, 64);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.06f, 1.0f));

	// Center the image
	void* texture_id = m_selectedEntry.icon ? reinterpret_cast<void*>(static_cast<intptr_t>(m_selectedEntry.icon->id())) : nullptr;

	ImGui::ImageButton("##thumb_img", reinterpret_cast<ImTextureID>(texture_id), thumbnail_size, ImVec2(0, 1), ImVec2(1, 0));

	// Now the image button is the drop target
	if (ImGui::BeginDragDropTarget()) {
		const auto* payload = ImGui::AcceptDragDropPayload("RESOURCE_STRUCT");
		if (payload && payload->Data) {
			auto* e = static_cast<Entry*>(payload->Data);
			ProcessDrop(e);
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::PopStyleColor();
}

void editor::ResourceSlot::RenderDetailsArea() {
	// Show entry info even if resource failed to load (we still have the path)
	if (!m_selectedEntry.relativePath.empty()) {
		ImGui::TextWrapped("%s", m_selectedEntry.name.c_str());

		// if (m_resource) {
		// 	ImVec4 badge_col(0.2f, 0.65f, 0.2f, 1.0f);
		// 	ImGui::SameLine();
		// 	ImGui::TextColored(badge_col, "[%s]", type_to_string(m_resource->GetResourceType()));
		// } else {
		// 	// Resource failed to load but we have the path
		// 	ImVec4 badge_col(0.65f, 0.4f, 0.2f, 1.0f);
		// 	ImGui::SameLine();
		// 	ImGui::TextColored(badge_col, "[Not Loaded]");
		// }

		ImGui::TextDisabled("%s", ToForwardSlashes(m_selectedEntry.relativePath.string()).c_str());

		if (ImGui::Button("Info")) {
			ImGui::OpenPopup("Resource Info");
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Reset")) {
			Entry e = resource::ResourceManager::CreateResourceSlotEntry(m_defaultPath);
			ProcessDrop(&e);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy Path")) {
			ImGui::SetClipboardText(ToForwardSlashes(m_selectedEntry.relativePath.string()).c_str());
		}

		if (ImGui::BeginPopup("Resource Info")) {
			ImGui::Text("Name: %s", m_selectedEntry.name.c_str());
			ImGui::Text("Path: %s", ToForwardSlashes(m_selectedEntry.relativePath.string()).c_str());
			ImGui::Text("Extension: %s", m_selectedEntry.extension.c_str());
			ImGui::Separator();
			ImGui::TextWrapped("TODO: Additional metadata");
			if (ImGui::Button("Close")) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(m_selectedEntry.name.c_str());
			ImGui::Separator();
			ImGui::TextUnformatted(ToForwardSlashes(m_selectedEntry.relativePath.string()).c_str());
			ImGui::EndTooltip();
		}

	} else {
		ImGui::TextDisabled("Drop a %s resource here", type_to_string(m_requiredType));
	}

	if (ImGui::BeginPopupContextItem("ResourceSlotContext")) {
		if (ImGui::MenuItem("Reset")) {
			Entry e = resource::ResourceManager::CreateResourceSlotEntry(m_defaultPath);
			ProcessDrop(&e);
		}
		if (!m_selectedEntry.relativePath.empty() && ImGui::MenuItem("Copy Path")) {
			ImGui::SetClipboardText(ToForwardSlashes(m_selectedEntry.relativePath.string()).c_str());
		}
		ImGui::EndPopup();
	}
}

void editor::ResourceSlot::RenderPopups() {
	std::string popupId = std::string("Invalid Resource Type##") + std::to_string(reinterpret_cast<uintptr_t>(this));

	if (m_showTypeErrorPopup) {
		ImGui::OpenPopup(popupId.c_str());
		m_showTypeErrorPopup = false;
	}

	if (ImGui::BeginPopupModal(popupId.c_str(), nullptr, ImGuiChildFlags_AlwaysAutoResize)) {
		// Use TextUnformatted to avoid treating the message as a format string
		ImGui::TextUnformatted(m_typeErrorMessage.c_str());
		ImGui::Separator();
		if (ImGui::Button("OK")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void editor::ResourceSlot::SetInitialResource(const std::string& default_path) {
	m_defaultPath = default_path;
	auto e = resource::ResourceManager::CreateResourceSlotEntry(std::filesystem::path(m_defaultPath));
	ProcessDrop(&e);
}

void editor::ResourceSlot::SetResource(const std::string& path) {
	auto e = resource::ResourceManager::CreateResourceSlotEntry(std::filesystem::path(path));
	ProcessDrop(&e);
}

void editor::ResourceSlot::Show() {
	EnsureRegistryInitialized();

	ImGui::Spacing();
	ImGui::BeginGroup();
	ImGui::Text(m_name.c_str());
	ImGui::Separator();

	const ImVec2 thumbnail_size(50, 64);
	const float vertical_padding = 5.0f;
	ImGui::PushID(this);

	ImGui::BeginChild("##ResourceSlotChild", ImVec2(0, thumbnail_size.y + vertical_padding * 2), false, ImGuiWindowFlags_NoScrollbar);

	RenderThumbnailArea();

	ImGui::SameLine();
	ImGui::BeginGroup();
	RenderDetailsArea();
	ImGui::EndGroup();

	ImGui::EndChild();
	ImGui::PopID();
	ImGui::EndGroup();

	RenderPopups();
}

bool editor::ResourceSlot::CheckCorrectType(Entry* res) const {
	EnsureRegistryInitialized();

	auto it = s_resourceRegistry.find(m_requiredType);
	if (it == s_resourceRegistry.end()) {
		TOAST_WARN("ResourceSlot: Unknown or unregistered resource type");
		return false;
	}

	const auto& exts = it->second.extensions;
	if (exts.empty()) {
		return true;    // accept any
	}

	std::string ext = ToLower(res->extension);
	auto found = std::find(exts.begin(), exts.end(), ext);
	if (found == exts.end()) {
		TOAST_WARN("ResourceSlot: Invalid resource type for {}: {}", static_cast<int>(m_requiredType), res->name);
		return false;
	}

	return true;
}
#endif
