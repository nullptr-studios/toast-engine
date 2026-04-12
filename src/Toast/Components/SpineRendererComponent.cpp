/// @file SpineRendererComponent.cpp
/// @author dario
/// @date 23/10/2025.

#include "Toast/Components/SpineRendererComponent.hpp"

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Time.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include "ResourceManager/Spine/SpineSkeletonRenderer.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Resources/Spine/SpineEvent.hpp"
#include "spine/Animation.h"
#include "spine/Bone.h"

/// TODO:SPINE RESOURCE SLOTS
void SpineRendererComponent::Init() {
	TransformComponent::Init();

	SetRunEarlyTick(false);
	SetRunLateTick(false);

	// shader and buffers
	m.shader = resource::LoadResource<renderer::Shader>("SHADERS/spine.shader");
	m.occlusionShader = resource::LoadResource<renderer::Shader>("SHADERS/occlusion.shader");

	// Reserve temp buffers to avoid allocations
	m.tempVerts.reserve(INITIAL_VERT_RESERVE);
	m.tempIndices.reserve(INITIAL_VERT_RESERVE * 3);

	m.eventHandler = std::make_unique<SpineEventHandler>(this);

	// Load resources either from persisted paths (preferred) or fallback to defaults
	if (!m.atlasPath.empty() && !m.skeletonDataPath.empty()) {
		auto atlas = resource::LoadResource<SpineAtlas>(m.atlasPath);
		m.skeletonData = resource::LoadResource<SpineSkeletonData>(m.skeletonDataPath, atlas);
	} else {
		m.atlasPath = "CHARS/PLAYER/ANIMATIONS/CH_Cat.atlas";
		m.skeletonDataPath = "CHARS/PLAYER/ANIMATIONS/CH_Cat.json";

		// Fallback to legacy defaults to keep previous behavior
		auto atlas = resource::LoadResource<SpineAtlas>(m.atlasPath);
		m.skeletonData = resource::LoadResource<SpineSkeletonData>(m.skeletonDataPath, atlas);
	}

	if (m.skeletonData) {
		m.skeleton = std::make_unique<spine::Skeleton>(m.skeletonData->GetSkeletonData());
		m.animationStateData = std::make_unique<spine::AnimationStateData>(m.skeletonData->GetSkeletonData());
		m.animationStateData->setDefaultMix(.4f);
		m.animationState = std::make_unique<spine::AnimationState>(m.animationStateData.get());
		m.animationState->setListener(m.eventHandler.get());

		// Initial update to ensure world transforms are valid
		m.skeleton->update(0.0f);
		m.skeleton->updateWorldTransform(spine::Physics_None);
#ifdef TOAST_EDITOR
		RefreshAnimationList();
		// Auto-select first animation if available
		if (!debug.animationNames.empty()) {
			debug.selectedAnimation = 0;
			m.animationState->setAnimation(0, debug.animationNames[debug.selectedAnimation].c_str(), debug.loopAnimation);
		}

		m.atlasResource.name("Atlas Resource");
		m.skeletonDataResource.name("Skeleton Data Resource");
#endif
	}

#ifdef TOAST_EDITOR
	m.atlasResource.SetInitialResource(m.atlasPath);
	m.skeletonDataResource.SetInitialResource(m.skeletonDataPath);
#endif
}

void SpineRendererComponent::LoadTextures() {
	m.shader->Use();
	m.shader->SetSampler("Texture", 0);

	renderer::IRendererBase::GetInstance()->AddTransparent(this);

	m.dynamicMesh.InitDynamicSpine();
}

void SpineRendererComponent::Begin() {
	TransformComponent::Begin();

	// reset skeleton
	m.skeleton->setToSetupPose();
}

void SpineRendererComponent::OnEnable() {
	if (auto* r = renderer::IRendererBase::GetInstance()) {
		r->DisableTransparent(this);
	}
}

void SpineRendererComponent::OnDisable() {
	if (auto* r = renderer::IRendererBase::GetInstance()) {
		r->EnableTransparent(this);
	}
}

void SpineRendererComponent::Tick() {
	// if (!OclussionVolume::isSphereOnPlanes(
	//         renderer::IRendererBase::GetInstance()->GetFrustumPlanes(), worldPosition(), 10.0f * glm::length(scale()) / 2.0f
	//     )) {
	// 	return;
	// }

	if (!m.skeleton || !m.animationState || !m.onScreen) {
		return;
	}

	double dt = Time::delta();

	m.animationState->update(dt);
	m.animationState->apply(*m.skeleton);

	for (const auto& [name, pos] : m.boneLocalOverrides) {
		spine::Bone* b = m.skeleton->findBone(name.c_str());
		if (!b) {
			continue;
		}
		b->setX(pos.x);
		b->setY(pos.y);
	}

	m.skeleton->update(dt);
	m.skeleton->updateWorldTransform(spine::Physics_Update);
}

#ifdef TOAST_EDITOR

void SpineRendererComponent::RefreshAnimationList() {
	debug.animationNames.clear();
	debug.selectedAnimation = -1;
	if (!m.skeletonData) {
		return;
	}
	spine::SkeletonData* data = m.skeletonData->GetSkeletonData();
	if (!data) {
		return;
	}

	const spine::Vector<spine::Animation*>& anims = data->getAnimations();
	debug.animationNames.reserve(anims.size());
	for (size_t i = 0; i < anims.size(); ++i) {
		spine::Animation* a = anims[i];
		if (a) {
			debug.animationNames.emplace_back(a->getName().buffer());
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
	m.atlasResource.Show();
	m.skeletonDataResource.Show();

	bool resources_changed = false;
	if (ImGui::Button("Load")) {
		if (m.atlasResource.GetResourcePath().empty() || m.skeletonDataResource.GetResourcePath().empty()) {
			TOAST_WARN("SpineRendererComponent::Inspector() Cannot load Spine resources: paths are empty");
			return;
		}

		// Persist paths similarly to AtlasRendererComponent
		m.atlasPath = m.atlasResource.GetResourcePath();
		m.skeletonDataPath = m.skeletonDataResource.GetResourcePath();

		auto atlas = resource::LoadResource<SpineAtlas>(m.atlasPath);
		m.skeletonData = resource::LoadResource<SpineSkeletonData>(m.skeletonDataPath, atlas);
		m.skeleton = std::make_unique<spine::Skeleton>(m.skeletonData->GetSkeletonData());
		m.animationStateData = std::make_unique<spine::AnimationStateData>(m.skeletonData->GetSkeletonData());
		m.animationState = std::make_unique<spine::AnimationState>(m.animationStateData.get());
		m.animationState->setListener(m.eventHandler.get());

		// Tick once
		double dt = Time::delta();

		m.animationState->update(dt);
		m.animationState->apply(*m.skeleton);

		m.skeleton->update(dt);
		m.skeleton->updateWorldTransform(spine::Physics_None);

		resources_changed = true;
	}

	if (resources_changed) {
		RefreshAnimationList();
		if (!debug.animationNames.empty()) {
			debug.selectedAnimation = 0;
			m.animationState->setAnimation(0, debug.animationNames[debug.selectedAnimation].c_str(), debug.loopAnimation);
		}
	}

	ImGui::Checkbox("2D Light Occluder", &m.isOccluder);
	ImGui::Checkbox("Casts Directional Shadow", &m.castsDirectionalShadow);
	ImGui::Checkbox("Draw to depth", &m.drawToDepth);

	ImGui::Separator();
	ImGui::Text("Animation Preview");

	if (debug.animationNames.empty()) {
		ImGui::Text("No animations found");
	} else {
		// Build preview label
		const char* current = (debug.selectedAnimation >= 0 && debug.selectedAnimation < (int)debug.animationNames.size())
		                          ? debug.animationNames[debug.selectedAnimation].c_str()
		                          : "<none>";
		if (ImGui::BeginCombo("##SpineAnimCombo", current)) {
			for (int i = 0; i < (int)debug.animationNames.size(); ++i) {
				bool selected = (debug.selectedAnimation == i);
				if (ImGui::Selectable(debug.animationNames[i].c_str(), selected)) {
					debug.selectedAnimation = i;

					if (m.animationState && m.skeleton) {
						m.animationState->setAnimation(0, debug.animationNames[i].c_str(), debug.loopAnimation);
					}
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::Checkbox("Loop", &debug.loopAnimation);
		ImGui::SameLine();
		if (ImGui::Button("Play")) {
			if (m.animationState && debug.selectedAnimation >= 0 && debug.selectedAnimation < (int)debug.animationNames.size()) {
				m.animationState->setAnimation(0, debug.animationNames[debug.selectedAnimation].c_str(), debug.loopAnimation);
			}
			debug.playing = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop")) {
			if (m.animationState) {
				m.animationState->clearTrack(0);
			}
			debug.playing = false;
		}
	}

	if (debug.playing) {
		double dt = Time::delta();

		m.animationState->update(dt);
		m.animationState->apply(*m.skeleton);

		m.skeleton->update(dt);
		m.skeleton->updateWorldTransform(spine::Physics_Update);
	}
}
#endif

void SpineRendererComponent::Destroy() {
	TransformComponent::Destroy();
	renderer::IRendererBase::GetInstance()->RemoveTransparent(this);
}

// optimize
void SpineRendererComponent::OnRender(renderer::IRenderablePass pass, const glm::mat4& precomputed_mat) noexcept {
	if (!enabled()) {
		return;
	}

	if (!m.skeleton) {
		return;
	}

	if (pass != renderer::IRenderablePass::GEOMETRY && pass != renderer::IRenderablePass::OCCLUSION &&
	    pass != renderer::IRenderablePass::DIRECTIONAL_SHADOW) {
		return;
	}

	// this is really unoptimiced lmao
	PROFILE_ZONE_C(0xFF0000);    // Red for rendering

	spine::RenderCommand* command = SpineSkeletonRenderer::getRenderer().render(*m.skeleton);

	const float z_step_cmd = 0.01f;       // inter-command z offset step
	const float z_step_vertex = 1e-4f;    // per-triangle/vertex z offset step
	float z_offset = 0.0f;

	const glm::mat4 model = GetWorldMatrix();
	const glm::mat4 mvp = precomputed_mat * model;

	// done just at occlusion step and reused
	if (pass == renderer::IRenderablePass::OCCLUSION || pass == renderer::IRenderablePass::DIRECTIONAL_SHADOW) {
		// First pass: collect all vertices to compute bounding box for frustum culling
		{
			// Compute dynamic bounding box
			m.dynamicMesh.ComputeSpineBoundingBox(m.tempVerts.data(), m.tempVerts.size());

			// Frustum culling using the dynamic AABB
			m.onScreen = OclussionVolume::isTransformedAABBOnPlanes(m.dynamicMesh.dynamicBoundingBox(), model);
		}

		const bool castsForPass = (pass == renderer::IRenderablePass::OCCLUSION) ? m.isOccluder : m.castsDirectionalShadow;
		if (!castsForPass) {
			return;
		}
	}

	// cache last bound texture to avoid redundant binds
	m.lastBoundTexture = 0;

	if (pass == renderer::IRenderablePass::GEOMETRY || pass == renderer::IRenderablePass::DIRECTIONAL_SHADOW) {
		m.shader->Use();
		m.shader->Set("transform", mvp);
	} else if (pass == renderer::IRenderablePass::OCCLUSION) {
		if (!m.isOccluder) {
			return;
		}

		m.occlusionShader->Use();
		m.occlusionShader->Set("gWorld", model);

		// set generic transform uniform
		m.occlusionShader->Set("gMVP", mvp);
	}

	if (pass != renderer::IRenderablePass::DIRECTIONAL_SHADOW && !m.onScreen) {
		return;
	}

	auto flush_batch = [&]() {
		if (m.tempIndices.empty()) {
			return;
		}
		// Update GPU buffer
		m.dynamicMesh.UpdateDynamicSpine(m.tempVerts.data(), m.tempVerts.size(), m.tempIndices.data(), m.tempIndices.size());
		// Draw  mesh
		m.dynamicMesh.DrawDynamicSpine(m.tempIndices.size());
	};

	m.tempVerts.clear();
	m.tempIndices.clear();

	while (command) {
		if (m.tempVerts.capacity() < static_cast<size_t>(command->numVertices)) {
			m.tempVerts.reserve(static_cast<size_t>(command->numVertices));
		}
		if (m.tempIndices.capacity() < static_cast<size_t>(command->numIndices)) {
			m.tempIndices.reserve(static_cast<size_t>(command->numIndices));
		}

		// Determine command texture
		std::shared_ptr<Texture>* tex_ptr = static_cast<std::shared_ptr<Texture>*>(command->texture);
		unsigned int tex_id = tex_ptr->get()->id();

		if (tex_id != m.lastBoundTexture && !m.tempIndices.empty()) {
			flush_batch();
		}

		// bind texture if needed
		if (tex_id != m.lastBoundTexture) {
			if (tex_id != 0) {
				tex_ptr->get()->Bind(0);
			}
			m.lastBoundTexture = tex_id;
		}

		// append vertices
		size_t start_vert = m.tempVerts.size();
		m.tempVerts.resize(start_vert + command->numVertices);
		for (int i = 0; i < command->numVertices; ++i) {
			auto& v = m.tempVerts[start_vert + i];
			v.position = glm::vec3(command->positions[(i * 2) + 0], command->positions[(i * 2) + 1], 0.0f);
			v.texCoord = glm::vec2(command->uvs[(i * 2) + 0], 1.0f - command->uvs[(i * 2) + 1]);
			v.colorABGR = command->colors[i];
		}

		// append indices
		size_t start_idx = m.tempIndices.size();
		m.tempIndices.resize(start_idx + command->numIndices);
		for (int i = 0; i < command->numIndices; ++i) {
			m.tempIndices[start_idx + i] = static_cast<uint16_t>(command->indices[i] + start_vert);
		}

		// Per-triangle Z layering
		for (int i = 0; i + 2 < command->numIndices; i += 3) {
			uint16_t ia = static_cast<uint16_t>(command->indices[i + 0] + start_vert);
			uint16_t ib = static_cast<uint16_t>(command->indices[i + 1] + start_vert);
			uint16_t ic = static_cast<uint16_t>(command->indices[i + 2] + start_vert);
			float z = z_offset;
			m.tempVerts[ia].position.z = z;
			m.tempVerts[ib].position.z = z;
			m.tempVerts[ic].position.z = z;
			z_offset += z_step_vertex;
		}

		// Increment Z between commands
		z_offset += z_step_cmd;

		command = command->next;
	}

	// flush any remaining geometry
	if (pass == renderer::IRenderablePass::DIRECTIONAL_SHADOW) {
		glDepthMask(GL_TRUE);
	} else if (m.drawToDepth) {
		glDepthMask(GL_TRUE);
	} else {
		glDepthMask(GL_FALSE);
	}

	if (pass != renderer::IRenderablePass::OCCLUSION && pass != renderer::IRenderablePass::DIRECTIONAL_SHADOW) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glDisable(GL_BLEND);
	}
	flush_batch();
}

void SpineRendererComponent::Load(json_t j, bool force_create) {
	PROFILE_ZONE_C(0x00FFFF);    // Cyan for deserialization
	TransformComponent::Load(j, force_create);
	if (j.contains("atlasResourcePath")) {
		m.atlasPath = j.at("atlasResourcePath").get<std::string>();
	}
	if (j.contains("skeletonDataResourcePath")) {
		m.skeletonDataPath = j.at("skeletonDataResourcePath").get<std::string>();
	}
	if (j.contains("isOccluder")) {
		m.isOccluder = j.at("isOccluder").get<bool>();
	}
	if (j.contains("castsDirectionalShadow")) {
		m.castsDirectionalShadow = j.at("castsDirectionalShadow").get<bool>();
	}
	if (j.contains("drawToDepth")) {
		m.drawToDepth = j.at("drawToDepth").get<bool>();
	}
}

json_t SpineRendererComponent::Save() const {
	PROFILE_ZONE_C(0x00FF00);    // Green for serialization
	json_t j = TransformComponent::Save();
	j["atlasResourcePath"] = m.atlasPath;
	j["skeletonDataResourcePath"] = m.skeletonDataPath;
	j["isOccluder"] = m.isOccluder;
	j["castsDirectionalShadow"] = m.castsDirectionalShadow;
	j["drawToDepth"] = m.drawToDepth;
	return j;
}

void SpineRendererComponent::PlayAnimation(std::string_view name, bool loop, int track) const {
	m.animationState->setAnimation(track, name.data(), loop);
}

void SpineRendererComponent::StopAnimation(int track) const {
	m.animationState->clearTrack(track);
}

void SpineRendererComponent::NextPlayAnimation(std::string_view name, bool loop, float delay, int track) const {
	m.animationState->addAnimation(track, name.data(), loop, delay);
}

void SpineRendererComponent::NextCrossFadeToDefault(float duration, int track) const {
	m.animationState->addEmptyAnimation(track, duration, 0);
}

void SpineRendererComponent::CrossFadeToDefault(float duration, int track) const {
	m.animationState->setEmptyAnimation(track, duration);
}

glm::vec2 SpineRendererComponent::GetBoneLocalPosition(std::string_view bone_name) const {
	if (!m.skeleton) {
		return glm::vec2(0.0f);
	}
	spine::Bone* bone = m.skeleton->findBone(bone_name.data());
	if (!bone) {
		TOAST_WARN("SpineRendererComponent::GetBoneLocalPosition() Bone \"{0}\" not found", bone_name);
		return glm::vec2(0.0f);
	}
	return { bone->getX(), bone->getY() };
}

void SpineRendererComponent::SetBoneLocalPosition(std::string_view bone_name, const glm::vec2& position) const {
	if (!m.skeleton) {
		return;
	}
	spine::Bone* bone = m.skeleton->findBone(bone_name.data());
	if (!bone) {
		TOAST_WARN("SpineRendererComponent::SetBoneLocalPosition() Bone \"{0}\" not found", bone_name);
		return;
	}
	// Apply immediately
	bone->setX(position.x);
	bone->setY(position.y);

	m.boneLocalOverrides[std::string(bone_name)] = position;
}

void SpineRendererComponent::ClearBoneLocalPositionOverride(std::string_view bone_name) const {
	m.boneLocalOverrides.erase(std::string(bone_name));
}

void SpineRendererComponent::ClearAllBoneLocalPositionOverrides() const {
	m.boneLocalOverrides.clear();
}

float SpineRendererComponent::GetBoneLocalRotation(std::string_view bone_name) const {
	if (!m.skeleton) {
		return 0.0f;
	}
	spine::Bone* bone = m.skeleton->findBone(bone_name.data());
	if (!bone) {
		TOAST_WARN("SpineRendererComponent::GetBoneLocalRotation() Bone \"{0}\" not found", bone_name);
		return 0.0f;
	}
	return bone->getRotation();
}

void SpineRendererComponent::SetBoneLocalRotation(std::string_view bone_name, float rotation_degrees) const {
	if (!m.skeleton) {
		return;
	}
	spine::Bone* bone = m.skeleton->findBone(bone_name.data());
	if (!bone) {
		TOAST_WARN("SpineRendererComponent::SetBoneLocalRotation() Bone \"{0}\" not found", bone_name);
		return;
	}
	bone->setRotation(rotation_degrees);
}

float SpineRendererComponent::GetBoneWorldRotation(std::string_view bone_name) const {
	if (!m.skeleton) {
		return 0.0f;
	}
	spine::Bone* bone = m.skeleton->findBone(bone_name.data());
	if (!bone) {
		TOAST_WARN("SpineRendererComponent::GetBoneWorldRotation() Bone \"{0}\" not found", bone_name);
		return 0.0f;
	}
	return bone->getWorldRotationX();
}

void SpineRendererComponent::SetBoneWorldRotation(std::string_view bone_name, float rotation_degrees) {    // NOLINT
	if (!m.skeleton) {
		return;
	}
	spine::Bone* bone = m.skeleton->findBone(bone_name.data());
	if (!bone) {
		TOAST_WARN("SpineRendererComponent::SetBoneWorldRotation() Bone \"{0}\" not found", bone_name);
		return;
	}

	// To set world rotation, we need to convert it to local rotation based on parent's world rotation
	float parent_world_rot = 0.0f;
	if (bone->getParent()) {
		parent_world_rot = bone->getParent()->getWorldRotationX();
	}
	float local_rot = rotation_degrees - parent_world_rot;
	bone->setRotation(local_rot);
}

glm::vec2 SpineRendererComponent::GetBoneWorldPosition(std::string_view bone_name) {
	if (!m.skeleton) {
		return glm::vec2(0.0f);
	}
	spine::Bone* bone = m.skeleton->findBone(bone_name.data());
	if (!bone) {
		TOAST_WARN("SpineRendererComponent::GetBoneWorldPosition() Bone \"{0}\" not found", bone_name);
		return glm::vec2(0.0f);
	}

	const glm::vec4 spine_local(bone->getWorldX(), bone->getWorldY(), 0.0f, 1.0f);
	const glm::vec4 world_pos = GetWorldMatrix() * spine_local;
	return { world_pos.x, world_pos.y };
}

glm::vec2 SpineRendererComponent::WorldPositionToSpineLocal(const glm::vec2& world_pos) {
	const glm::vec4 wp(world_pos.x, world_pos.y, 0.0f, 1.0f);
	const glm::vec4 spine_local = glm::inverse(GetWorldMatrix()) * wp;
	return { spine_local.x, spine_local.y };
}

void SpineRendererComponent::OnAnimationEvent(
    std::string_view animation_name, int track, const std::string_view& event_name, int int_value, float float_value, std::string_view string_value
) {
	event::Send(new SpineEvent(id(), animation_name, track, event_name, int_value, float_value, string_value));
	// TOAST_TRACE("Spine Event Sent!");
}
