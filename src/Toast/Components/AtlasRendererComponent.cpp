/// @file AtlasRendererComponent.cpp
/// @author dario
/// @date 21/11/2025.

#include "Toast/Components/AtlasRendererComponent.hpp"

#include "ResourceManager/Spine/SpineSkeletonRenderer.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "spine/SkeletonRenderer.h"
#include "spine/Skin.h"
#include "spine/Slot.h"
#include "spine/SlotData.h"

#include <algorithm>
#include <unordered_set>

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

void AtlasRendererComponent::Init() {
	m_shader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/spine.shader");

	// Reserve temp buffers to avoid allocations
	m_tempVerts.reserve(INITIAL_VERT_RESERVE);
	m_tempIndices.reserve(INITIAL_VERT_RESERVE * 3);

	if (!m_atlasPath.empty() && !m_skeletonDataPath.empty()) {
		auto atlas = resource::ResourceManager::GetInstance()->LoadResource<SpineAtlas>(m_atlasPath);
		m_skeletonData = resource::ResourceManager::GetInstance()->LoadResource<SpineSkeletonData>(m_skeletonDataPath, atlas);
		m_skeleton = std::make_unique<spine::Skeleton>(m_skeletonData->GetSkeletonData());

		// enumerate attachments
		EnumerateAttachmentNames();

		// If a previously saved atlas object exists, select and apply it
		if (!m_atlasObject.empty() && !m_attachmentNames.empty()) {
			auto it = std::find(m_attachmentNames.begin(), m_attachmentNames.end(), m_atlasObject);
			if (it != m_attachmentNames.end()) {
				m_selectedAttachment = static_cast<int>(std::distance(m_attachmentNames.begin(), it));
				SetOnlyAttachmentByName(m_atlasObject);
			} else {
				m_selectedAttachment = -1;
			}
		}

		// Tick once
		m_skeleton->update(0.16f);
		m_skeleton->updateWorldTransform(spine::Physics_None);
	}

	if (!m_atlasObject.empty()) {
		SetOnlyAttachmentByName(m_atlasObject);
	}
#ifdef TOAST_EDITOR
	m_atlasResource.SetInitialResource(m_atlasPath);
	m_skeletonDataResource.SetInitialResource(m_skeletonDataPath);
#endif
}

void AtlasRendererComponent::EnumerateAttachmentNames() {
	m_attachmentNames.clear();
	if (!m_skeletonData) {
		return;
	}

	auto* const data = m_skeletonData->GetSkeletonData();
	if (!data) {
		return;
	}

	std::unordered_set<std::string> names;

	for (int s = 0; s < data->getSkins().size(); ++s) {
		auto* const skin = data->getSkins()[s];
		auto i = skin->getAttachments();
		while (i.hasNext()) {
			auto& entry = i.next();
			if (entry._name.buffer()) {
				names.emplace(entry._name.buffer());
			}
		}
	}

	// fallback
	//  if (names.empty()) {
	//  	for (int i = 0; i < data->getSlots().size(); ++i) {
	//  		auto slot = data->getSlots()[i];
	//  	}
	//  }

	// Move into vector and sort
	m_attachmentNames.assign(names.begin(), names.end());
	std::ranges::sort(m_attachmentNames);

	// Reset selection if current selected not valid
	if (m_selectedAttachment >= static_cast<int>(m_attachmentNames.size())) {
		m_selectedAttachment = -1;
	}
}

void AtlasRendererComponent::SetOnlyAttachmentByName(const std::string& name) {
	if (!m_skeleton || !m_skeletonData) {
		return;
	}

	auto* data = m_skeletonData->GetSkeletonData();
	if (!data) {
		return;
	}

	// clear all attachments
	for (int i = 0; i < m_skeleton->getSlots().size(); ++i) {
		m_skeleton->getSlots()[i]->setAttachment(nullptr);
	}

	// Find the attachment entry
	for (int s = 0; s < data->getSkins().size(); ++s) {
		auto* const skin = data->getSkins()[s];
		auto i = skin->getAttachments();
		while (i.hasNext()) {
			if (auto& entry = i.next(); entry._name.buffer() && name == entry._name.buffer()) {
				int slot_index = entry._slotIndex;
				spine::Slot* slot = nullptr;
				if (slot_index >= 0 && slot_index < m_skeleton->getSlots().size()) {
					slot = m_skeleton->getSlots()[slot_index];
				}
				if (slot) {
					slot->setAttachment(entry._attachment);
					m_skeleton->updateWorldTransform(spine::Physics_None);
					UpdateMeshBounds();
					return;
				}
			}
		}
	}

	for (int i = 0; i < m_skeleton->getSlots().size(); ++i) {
		auto* slot = m_skeleton->getSlots()[i];
		if (slot->getData().getName().buffer() && name == slot->getData().getName().buffer()) {
			// attempt to get attachment with same name
			if (auto *const att = m_skeleton->getAttachment(slot->getData().getName().buffer(), name.c_str())) {
				slot->setAttachment(att);
				m_skeleton->updateWorldTransform(spine::Physics_None);
				UpdateMeshBounds();
				return;
			}
		}
	}
}

void AtlasRendererComponent::OnRender(const glm::mat4& precomputed_mat) noexcept {
	if (!enabled()) {
		return;
	}

	if (!m_skeleton) {
		return;
	}
	
	const glm::mat4 model = GetWorldMatrix();
	
	// Frustum culling using the dynamic AABB
	const auto& frustumPlanes = renderer::IRendererBase::GetInstance()->GetFrustumPlanes();
	if (!OclussionVolume::isTransformedAABBOnPlanes(frustumPlanes, m_dynamicMesh.dynamicBoundingBox(), model)) {
		return;    // Outside frustum, skip rendering
	}

	PROFILE_ZONE;

	spine::RenderCommand* command = SpineSkeletonRenderer::getRenderer().render(*m_skeleton);

	const float z_step_cmd = 0.01f;       // inter-command z offset step
	const float z_step_vertex = 1e-4f;    // per-triangle/vertex z offset step
	float z_offset = 0.0f;

	const glm::mat4 mvp = precomputed_mat * model;
	

	m_shader->Use();
	m_shader->Set("transform", mvp);

	// Reuse temporary buffers
	m_tempVerts.clear();
	m_tempIndices.clear();

	// cache last bound texture to avoid redundant binds
	m_lastBoundTexture = 0;

	auto flush_batch = [&]() {
		if (m_tempIndices.empty()) {
			return;
		}
		// Update GPU buffer
		m_dynamicMesh.UpdateDynamicSpine(m_tempVerts.data(), m_tempVerts.size(), m_tempIndices.data(), m_tempIndices.size());
		// Draw  mesh
		m_dynamicMesh.DrawDynamicSpine(m_tempIndices.size());
		// clear for next batch
		m_tempVerts.clear();
		m_tempIndices.clear();
	};

	while (command) {
		if (m_tempVerts.capacity() < static_cast<size_t>(command->numVertices)) {
			m_tempVerts.reserve(static_cast<size_t>(command->numVertices));
		}
		if (m_tempIndices.capacity() < static_cast<size_t>(command->numIndices)) {
			m_tempIndices.reserve(static_cast<size_t>(command->numIndices));
		}

		// Determine command texture
		std::shared_ptr<Texture>* tex_ptr = static_cast<std::shared_ptr<Texture>*>(command->texture);
		unsigned int tex_id = tex_ptr->get()->id();

		if (tex_id != m_lastBoundTexture && !m_tempIndices.empty()) {
			flush_batch();
		}

		// bind texture if needed
		if (tex_id != m_lastBoundTexture) {
			if (tex_id != 0) {
				tex_ptr->get()->Bind(0);
			}
			m_lastBoundTexture = tex_id;
		}

		// append vertices
		size_t start_vert = m_tempVerts.size();
		m_tempVerts.resize(start_vert + command->numVertices);
		for (int i = 0; i < command->numVertices; ++i) {
			auto& v = m_tempVerts[start_vert + i];
			v.position = glm::vec3(command->positions[(i * 2) + 0], command->positions[(i * 2) + 1], 0.0f);
			v.texCoord = glm::vec2(command->uvs[(i * 2) + 0], command->uvs[(i * 2) + 1]);
			v.colorABGR = command->colors[i];
		}

		// append indices
		size_t start_idx = m_tempIndices.size();
		m_tempIndices.resize(start_idx + command->numIndices);
		for (int i = 0; i < command->numIndices; ++i) {
			m_tempIndices[start_idx + i] = static_cast<uint16_t>(command->indices[i] + start_vert);
		}

		// Per-triangle Z layering
		for (size_t i = 0; i + 2 < command->numIndices; i += 3) {
			uint16_t ia = static_cast<uint16_t>(command->indices[i + 0] + start_vert);
			uint16_t ib = static_cast<uint16_t>(command->indices[i + 1] + start_vert);
			uint16_t ic = static_cast<uint16_t>(command->indices[i + 2] + start_vert);
			float z = z_offset;
			m_tempVerts[ia].position.z = z;
			m_tempVerts[ib].position.z = z;
			m_tempVerts[ic].position.z = z;
			z_offset += z_step_vertex;
		}

		// Increment Z between commands
		z_offset += z_step_cmd;

		command = command->next;
	}

	// flush any remaining geometry
	flush_batch();
}

void AtlasRendererComponent::LoadTextures() {
	m_shader->Use();
	m_shader->SetSampler("Texture", 0);

	renderer::IRendererBase::GetInstance()->AddRenderable(this);

	m_dynamicMesh.InitDynamicSpine();
}

void AtlasRendererComponent::Load(json_t j, bool force_create) {
	TransformComponent::Load(j, force_create);
	if (j.contains("atlasResourcePath")) {
		m_atlasPath = j.at("atlasResourcePath").get<std::string>();
	}
	if (j.contains("skeletonDataResourcePath")) {
		m_skeletonDataPath = j.at("skeletonDataResourcePath").get<std::string>();
	}
	if (j.contains("atlasObject")) {
		m_atlasObject = j.at("atlasObject").get<std::string>();

		// If attachment names already enumerated (e.g. resource was loaded earlier), update selection
		if (!m_atlasObject.empty() && !m_attachmentNames.empty()) {
			auto it = std::find(m_attachmentNames.begin(), m_attachmentNames.end(), m_atlasObject);
			if (it != m_attachmentNames.end()) {
				m_selectedAttachment = static_cast<int>(std::distance(m_attachmentNames.begin(), it));
				SetOnlyAttachmentByName(m_atlasObject);
			} else {
				m_selectedAttachment = -1;
			}
		}
	}
}

void AtlasRendererComponent::UpdateMeshBounds() {
	
	spine::RenderCommand* command = SpineSkeletonRenderer::getRenderer().render(*m_skeleton);
	// collect all vertices to compute bounding box for frustum culling
	{
		spine::RenderCommand* cmd = command;
		size_t totalVerts = 0;
		while (cmd) {
			totalVerts += cmd->numVertices;
			cmd = cmd->next;
		}

		// Build temporary vertex positions for bounding box computation
		m_tempVerts.reserve(totalVerts);
		cmd = command;
		while (cmd) {
			for (int i = 0; i < cmd->numVertices; ++i) {
				renderer::SpineVertex v{};
				v.position = glm::vec3(cmd->positions[(i * 2) + 0], cmd->positions[(i * 2) + 1], 0.0f);
				v.texCoord = glm::vec2(cmd->uvs[(i * 2) + 0], cmd->uvs[(i * 2) + 1]);
				v.colorABGR = cmd->colors[i];
				m_tempVerts.push_back(v);
			}
			cmd = cmd->next;
		}

		// Compute dynamic bounding box
		m_dynamicMesh.ComputeSpineBoundingBox(m_tempVerts.data(), m_tempVerts.size());
		
	}

	// Reset buffers for actual rendering pass
	m_tempVerts.clear();
	m_tempIndices.clear();
}

#ifdef TOAST_EDITOR

// ImGui helper
static bool ItemsGetter(void* data, int idx, const char** out_text) {
	auto vec = static_cast<std::vector<std::string>*>(data);
	if (idx < 0 || idx >= static_cast<int>(vec->size())) {
		return false;
	}
	*out_text = vec->at(idx).c_str();
	return true;
}

void AtlasRendererComponent::Inspector() {
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(20);
		TransformComponent::Inspector();
		ImGui::Unindent(20);
	}
	ImGui::Spacing();

	m_atlasResource.Show();
	m_skeletonDataResource.Show();

	if (ImGui::Button("Load")) {
		if (m_atlasResource.GetResourcePath().empty() || m_skeletonDataResource.GetResourcePath().empty()) {
			TOAST_WARN("AtlasRendererComponent::Inspector() Cannot load Spine resources: paths are empty");
			return;
		}

		m_skeletonDataPath = m_skeletonDataResource.GetResourcePath();
		m_atlasPath = m_atlasResource.GetResourcePath();

		auto atlas = resource::ResourceManager::GetInstance()->LoadResource<SpineAtlas>(m_atlasResource.GetResourcePath());
		m_skeletonData = resource::ResourceManager::GetInstance()->LoadResource<SpineSkeletonData>(m_skeletonDataResource.GetResourcePath(), atlas);
		if (m_skeletonData->GetResourceState() == resource::ResourceState::FAILED) {
			TOAST_ERROR("AtlasRendererComponent::Inspector() Failed loading SpineSkeletonData from path \"{0}\"", m_skeletonDataResource.GetResourcePath());
			return;
		}
		m_skeleton = std::make_unique<spine::Skeleton>(m_skeletonData->GetSkeletonData());

		// enumerate attachments
		EnumerateAttachmentNames();

		// apply saved selection if present
		if (!m_atlasObject.empty() && !m_attachmentNames.empty()) {
			auto it = std::find(m_attachmentNames.begin(), m_attachmentNames.end(), m_atlasObject);
			if (it != m_attachmentNames.end()) {
				m_selectedAttachment = static_cast<int>(std::distance(m_attachmentNames.begin(), it));
				SetOnlyAttachmentByName(m_atlasObject);
			} else {
				m_selectedAttachment = -1;
			}
		}

		// Tick once
		m_skeleton->update(0.16f);
		m_skeleton->updateWorldTransform(spine::Physics_Update);
	}

	ImGui::Separator();

	if (!m_skeleton) {
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "No skeleton loaded");
		return;
	}

	ImGui::Text("Attachments: %d", static_cast<int>(m_attachmentNames.size()));
	// Attachment picker
	if (!m_attachmentNames.empty()) {
		const char* preview = (m_selectedAttachment >= 0 && m_selectedAttachment < static_cast<int>(m_attachmentNames.size()))
		                          ? m_attachmentNames[m_selectedAttachment].c_str()
		                          : "<none>";
		if (ImGui::BeginCombo("Attachment", preview)) {
			for (int i = 0; i < static_cast<int>(m_attachmentNames.size()); ++i) {
				const bool selected = (m_selectedAttachment == i);
				if (ImGui::Selectable(m_attachmentNames[i].c_str(), selected)) {
					m_selectedAttachment = i;
					// set only this attachment
					SetOnlyAttachmentByName(m_attachmentNames[i]);
					m_atlasObject = m_attachmentNames[i];
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
}

#endif

json_t AtlasRendererComponent::Save() const {
	json_t j = TransformComponent::Save();
	j["atlasResourcePath"] = m_atlasPath;
	j["skeletonDataResourcePath"] = m_skeletonDataPath;
	j["atlasObject"] = m_atlasObject;
	return j;
}

void AtlasRendererComponent::Destroy() {
	renderer::IRendererBase::GetInstance()->RemoveRenderable(this);
}
