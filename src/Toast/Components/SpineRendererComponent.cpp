/// @file SpineRendererComponent.cpp
/// @author dario
/// @date 23/10/2025.

#include "../../ResourceManager/Spine/SpineSkeletonRenderer.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Time.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include "Toast/Components/AtlasRendererComponent.hpp"
#include "Toast/Components/SpineRendererComponent.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Resources/Spine/SpineEvent.hpp"
#include "spine/Animation.h"
#include "spine/Attachment.h"
#include "spine/Bone.h"

/// TODO:SPINE RESOURCE SLOTS
void SpineRendererComponent::Init() {
	TransformComponent::Init();

	// shader and buffers
	m_shader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/spine.shader");
	// Reserve temp buffers to avoid allocations
	m_tempVerts.reserve(INITIAL_VERT_RESERVE);
	m_tempIndices.reserve(INITIAL_VERT_RESERVE * 3);

	m_eventHandler = std::make_unique<SpineEventHandler>(this);

	// Load resources either from persisted paths (preferred) or fallback to defaults
	if (!m_atlasPath.empty() && !m_skeletonDataPath.empty()) {
		auto atlas = resource::ResourceManager::GetInstance()->LoadResource<SpineAtlas>(m_atlasPath);
		m_skeletonData = resource::ResourceManager::GetInstance()->LoadResource<SpineSkeletonData>(m_skeletonDataPath, atlas);
	} else {
		m_atlasPath = "animations/player/Player-unfinished.atlas";
		m_skeletonDataPath = "animations/player/Player-unfinished.json";

		// Fallback to legacy defaults to keep previous behavior
		auto atlas = resource::ResourceManager::GetInstance()->LoadResource<SpineAtlas>(m_atlasPath);
		m_skeletonData = resource::ResourceManager::GetInstance()->LoadResource<SpineSkeletonData>(m_skeletonDataPath, atlas);
	}

	if (m_skeletonData) {
		m_skeleton = std::make_unique<spine::Skeleton>(m_skeletonData->GetSkeletonData());
		m_animationStateData = std::make_unique<spine::AnimationStateData>(m_skeletonData->GetSkeletonData());
		m_animationStateData->setDefaultMix(.5f);
		m_animationState = std::make_unique<spine::AnimationState>(m_animationStateData.get());
		m_animationState->setListener(m_eventHandler.get());

		// Initial update to ensure world transforms are valid
		m_skeleton->update(0.0f);
		m_skeleton->updateWorldTransform(spine::Physics_None);
#ifdef TOAST_EDITOR
		RefreshAnimationList();
		// Auto-select first animation if available
		if (!m_animationNames.empty()) {
			m_selectedAnimation = 0;
			m_animationState->setAnimation(0, m_animationNames[m_selectedAnimation].c_str(), m_loopAnimation);
		}

		m_atlasResource.name("Atlas Resource");
		m_skeletonDataResource.name("Skeleton Data Resource");
#endif
	}

#ifdef TOAST_EDITOR
	m_atlasResource.SetInitialResource(m_atlasPath);
	m_skeletonDataResource.SetInitialResource(m_skeletonDataPath);
#endif
}

void SpineRendererComponent::LoadTextures() {
	m_shader->Use();
	m_shader->SetSampler("Texture", 0);

	renderer::IRendererBase::GetInstance()->AddRenderable(this);

	m_dynamicMesh.InitDynamicSpine();
}

void SpineRendererComponent::Begin() {
	TransformComponent::Begin();
}

void SpineRendererComponent::Tick() {
	// if (!OclussionVolume::isSphereOnPlanes(
	//         renderer::IRendererBase::GetInstance()->GetFrustumPlanes(), worldPosition(), 10.0f * glm::length(scale()) / 2.0f
	//     )) {
	// 	return;
	// }

	if (!m_skeleton || !m_animationState) {
		return;
	}

	double dt = Time::delta();

	m_animationState->update(dt);
	m_animationState->apply(*m_skeleton);

	m_skeleton->update(dt);
	m_skeleton->updateWorldTransform(spine::Physics_Update);
}

#ifdef TOAST_EDITOR

void SpineRendererComponent::RefreshAnimationList() {
	m_animationNames.clear();
	m_selectedAnimation = -1;
	if (!m_skeletonData) {
		return;
	}
	spine::SkeletonData* data = m_skeletonData->GetSkeletonData();
	if (!data) {
		return;
	}

	const spine::Vector<spine::Animation*>& anims = data->getAnimations();
	m_animationNames.reserve(anims.size());
	for (size_t i = 0; i < anims.size(); ++i) {
		spine::Animation* a = anims[i];
		if (a) {
			m_animationNames.emplace_back(a->getName().buffer());
		}
	}
}
#endif

#ifdef TOAST_EDITOR
void SpineRendererComponent::Inspector() {
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(20);
		TransformComponent::Inspector();
		ImGui::Unindent(20);
	}
	ImGui::Spacing();
	// Animation controls
	m_atlasResource.Show();
	m_skeletonDataResource.Show();

	bool resourcesChanged = false;
	if (ImGui::Button("Load")) {
		if (m_atlasResource.GetResourcePath().empty() || m_skeletonDataResource.GetResourcePath().empty()) {
			TOAST_WARN("SpineRendererComponent::Inspector() Cannot load Spine resources: paths are empty");
			return;
		}

		// Persist paths similarly to AtlasRendererComponent
		m_atlasPath = m_atlasResource.GetResourcePath();
		m_skeletonDataPath = m_skeletonDataResource.GetResourcePath();

		auto atlas = resource::ResourceManager::GetInstance()->LoadResource<SpineAtlas>(m_atlasPath);
		m_skeletonData = resource::ResourceManager::GetInstance()->LoadResource<SpineSkeletonData>(m_skeletonDataPath, atlas);
		m_skeleton = std::make_unique<spine::Skeleton>(m_skeletonData->GetSkeletonData());
		m_animationStateData = std::make_unique<spine::AnimationStateData>(m_skeletonData->GetSkeletonData());
		m_animationState = std::make_unique<spine::AnimationState>(m_animationStateData.get());
		m_animationState->setListener(m_eventHandler.get());

		// Tick once
		double dt = Time::delta();

		m_animationState->update(dt);
		m_animationState->apply(*m_skeleton);

		m_skeleton->update(dt);
		m_skeleton->updateWorldTransform(spine::Physics_None);

		resourcesChanged = true;
	}

	if (resourcesChanged) {
		RefreshAnimationList();
		if (!m_animationNames.empty()) {
			m_selectedAnimation = 0;
			m_animationState->setAnimation(0, m_animationNames[m_selectedAnimation].c_str(), m_loopAnimation);
		}
	}

	ImGui::Separator();
	ImGui::Text("Animation Preview");

	if (m_animationNames.empty()) {
		ImGui::Text("No animations found");
	} else {
		// Build preview label
		const char* current =
		    (m_selectedAnimation >= 0 && m_selectedAnimation < (int)m_animationNames.size()) ? m_animationNames[m_selectedAnimation].c_str() : "<none>";
		if (ImGui::BeginCombo("##SpineAnimCombo", current)) {
			for (int i = 0; i < (int)m_animationNames.size(); ++i) {
				bool selected = (m_selectedAnimation == i);
				if (ImGui::Selectable(m_animationNames[i].c_str(), selected)) {
					m_selectedAnimation = i;

					if (m_animationState && m_skeleton) {
						m_animationState->setAnimation(0, m_animationNames[i].c_str(), m_loopAnimation);
					}
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::Checkbox("Loop", &m_loopAnimation);
		ImGui::SameLine();
		if (ImGui::Button("Play")) {
			if (m_animationState && m_selectedAnimation >= 0 && m_selectedAnimation < (int)m_animationNames.size()) {
				m_animationState->setAnimation(0, m_animationNames[m_selectedAnimation].c_str(), m_loopAnimation);
			}
			m_playing = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop")) {
			if (m_animationState) {
				m_animationState->clearTrack(0);
			}
			m_playing = false;
		}
	}

	if (m_playing) {
		double dt = Time::delta();

		m_animationState->update(dt);
		m_animationState->apply(*m_skeleton);

		m_skeleton->update(dt);
		m_skeleton->updateWorldTransform(spine::Physics_Update);
	}
}
#endif

void SpineRendererComponent::Destroy() {
	TransformComponent::Destroy();
	renderer::IRendererBase::GetInstance()->RemoveRenderable(this);
}

void SpineRendererComponent::OnRender(const glm::mat4& precomputed_mat) noexcept {
	if (!enabled()) {
		return;
	}

	if (!m_skeleton) {
		return;
	}

	PROFILE_ZONE;

	spine::RenderCommand* command = SpineSkeletonRenderer::getRenderer().render(*m_skeleton);

	const float z_step_cmd = 0.01f;       // inter-command z offset step
	const float z_step_vertex = 1e-4f;    // per-triangle/vertex z offset step
	float z_offset = 0.0f;

	const glm::mat4 model = GetWorldMatrix();
	const glm::mat4 mvp = precomputed_mat * model;

	m_shader->Use();
	m_shader->Set("transform", mvp);

	// Reuse temporary buffers
	m_tempVerts.clear();
	m_tempIndices.clear();

	// First pass: collect all vertices to compute bounding box for frustum culling
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
				renderer::SpineVertex v {};
				v.position = glm::vec3(cmd->positions[(i * 2) + 0], cmd->positions[(i * 2) + 1], 0.0f);
				v.texCoord = glm::vec2(cmd->uvs[(i * 2) + 0], cmd->uvs[(i * 2) + 1]);
				v.colorABGR = cmd->colors[i];
				m_tempVerts.push_back(v);
			}
			cmd = cmd->next;
		}

		// Compute dynamic bounding box
		m_dynamicMesh.ComputeSpineBoundingBox(m_tempVerts.data(), m_tempVerts.size());

		// Frustum culling using the dynamic AABB
		const auto& frustumPlanes = renderer::IRendererBase::GetInstance()->GetFrustumPlanes();
		if (!OclussionVolume::isTransformedAABBOnPlanes(frustumPlanes, m_dynamicMesh.dynamicBoundingBox(), model)) {
			return;    // Outside frustum, skip rendering
		}
	}

	// Reset buffers for actual rendering pass
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

void SpineRendererComponent::Load(json_t j, bool force_create) {
	TransformComponent::Load(j, force_create);
	if (j.contains("atlasResourcePath")) {
		m_atlasPath = j.at("atlasResourcePath").get<std::string>();
	}
	if (j.contains("skeletonDataResourcePath")) {
		m_skeletonDataPath = j.at("skeletonDataResourcePath").get<std::string>();
	}
}

json_t SpineRendererComponent::Save() const {
	json_t j = TransformComponent::Save();
	j["atlasResourcePath"] = m_atlasPath;
	j["skeletonDataResourcePath"] = m_skeletonDataPath;
	return j;
}

void SpineRendererComponent::PlayAnimation(const std::string_view& name, bool loop, int track) const {
	m_animationState->setAnimation(track, name.data(), loop);
}

void SpineRendererComponent::StopAnimation(int track) const {
	m_animationState->clearTrack(track);
}

void SpineRendererComponent::NextCrossFadeToDefault(float duration, int track) const {
	m_animationState->addEmptyAnimation(track, duration, 0);
}

void SpineRendererComponent::CrossFadeToDefault(float duration, int track) const {
	m_animationState->setEmptyAnimation(track, duration);
}

glm::vec2 SpineRendererComponent::GetBoneLocalPosition(const std::string_view& boneName) const {
	if (!m_skeleton) {
		return glm::vec2(0.0f);
	}
	spine::Bone* bone = m_skeleton->findBone(boneName.data());
	if (!bone) {
		TOAST_WARN("SpineRendererComponent::GetBoneLocalPosition() Bone \"{0}\" not found", boneName);
		return glm::vec2(0.0f);
	}
	return glm::vec2(bone->getX(), bone->getY());
}

void SpineRendererComponent::SetBoneLocalPosition(const std::string_view& boneName, const glm::vec2& position) const {
	if (!m_skeleton) {
		return;
	}
	spine::Bone* bone = m_skeleton->findBone(boneName.data());
	if (!bone) {
		TOAST_WARN("SpineRendererComponent::SetBoneLocalPosition() Bone \"{0}\" not found", boneName);
		return;
	}
	bone->setX(position.x);
	bone->setY(position.y);
}

void SpineRendererComponent::OnAnimationEvent(
    const std::string_view& animationName, int track, const std::string_view& eventName, int intValue, float floatValue,
    const std::string_view& stringValue
) {
	event::Send(new SpineEvent(id(), animationName, track, eventName, intValue, floatValue, stringValue));
	TOAST_TRACE("Spine Event Sent!");
}
