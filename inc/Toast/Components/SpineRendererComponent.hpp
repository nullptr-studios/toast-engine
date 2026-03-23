/// @file SpineRendererComponent.hpp
/// @author dario
/// @date 23/10/2025.

#pragma once
#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceSlot.hpp"
#include "Toast/Resources/Spine/SpineEventHandler.hpp"
#include "Toast/Resources/Spine/SpineSkeletonData.hpp"

#include <spine/AnimationState.h>
#include <spine/AnimationStateData.h>
#include <spine/Skeleton.h>
#include <unordered_map>

class SpineRendererComponent : public IRenderable {
public:
	REGISTER_TYPE(SpineRendererComponent);

	void Init() override;

	void LoadTextures() override;

	void Begin() override;
	void Tick() override;
	void OnEnable() override;
	void OnDisable() override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	void Destroy() override;

	void OnRender(renderer::IRenderablePass pass, const glm::mat4&) noexcept override;

	void SetSkeletonData(const std::shared_ptr<SpineSkeletonData>& data) {
		m.skeletonData = data;
		if (m.skeletonData) {
			m.skeleton = std::make_unique<spine::Skeleton>(m.skeletonData->GetSkeletonData());
			m.animationStateData = std::make_unique<spine::AnimationStateData>(m.skeletonData->GetSkeletonData());
			m.animationStateData->setDefaultMix(.4f);
			m.animationState = std::make_unique<spine::AnimationState>(m.animationStateData.get());
			m.animationState->setListener(m.eventHandler.get());

			// Initial update to ensure world transforms are valid
			m.skeleton->update(0.0f);
			m.skeleton->updateWorldTransform(spine::Physics_None);
		}
	}

	void ResetSkeletonToSetupPose() const {
		if (m.skeleton) {
			m.skeleton->setToSetupPose();
			m.skeleton->updateWorldTransform(spine::Physics_None);
		}
	}

	spine::AnimationStateData* GetSkeletonData() const {
		return m.animationStateData.get();
	}

	void Load(json_t j, bool force_create = true) override;
	json_t Save() const override;

	void PlayAnimation(std::string_view name, bool loop, int track = 0) const;
	void StopAnimation(int track = 0) const;
	void NextPlayAnimation(std::string_view name, bool loop, float delay, int track = 0) const;

	void NextCrossFadeToDefault(float duration, int track = 0) const;
	void CrossFadeToDefault(float duration, int track = 0) const;

	// Bone helpers
	glm::vec2 GetBoneLocalPosition(std::string_view bone_name) const;
	void SetBoneLocalPosition(std::string_view bone_name, const glm::vec2& position) const;

	float GetBoneLocalRotation(std::string_view bone_name) const;
	void SetBoneLocalRotation(std::string_view bone_name, float rotation_degrees) const;

	float GetBoneWorldRotation(std::string_view bone_name) const;
	void SetBoneWorldRotation(std::string_view bone_name, float rotation_degrees);

	/// @brief Returns the bone's world position (after applying the component's world transform).
	/// Useful for attaching game objects (e.g. weapon actors) to spine bones.
	glm::vec2 GetBoneWorldPosition(std::string_view bone_name);

	/// @brief Converts a 2-D world-space position into spine root-local space.
	glm::vec2 WorldPositionToSpineLocal(const glm::vec2& world_pos);

	void ClearBoneLocalPositionOverride(std::string_view bone_name) const;
	void ClearAllBoneLocalPositionOverrides() const;

	void SetIsOccluder(bool value) {
		m.isOccluder = value;
	}

	// Events
	virtual void OnAnimationStart(std::string_view /*animation_name*/, int /*track*/) { }

	virtual void OnAnimationCompleted(std::string_view /*animation_name*/, int /*track*/) { }

	virtual void OnAnimationEnd(std::string_view /*animation_name*/, int /*track*/) { }

	virtual void OnAnimationInterrupted(std::string_view /*animation_name*/, int /*track*/) { }

	virtual void OnAnimationDispose(std::string_view /*animation_name*/, int /*track*/) { }

	virtual void OnAnimationEvent(
	    std::string_view animation_name, int track, const std::string_view& event_name, int int_value, float float_value, std::string_view string_value
	);

private:
	static constexpr size_t INITIAL_VERT_RESERVE = 1024;

	struct {
		std::unique_ptr<SpineEventHandler> eventHandler;

		editor::ResourceSlot atlasResource { resource::ResourceType::SPINE_ATLAS };
		editor::ResourceSlot skeletonDataResource { resource::ResourceType::SPINE_SKELETON_DATA };

		// Persisted resource paths (mirrors AtlasRendererComponent)
		std::string atlasPath;
		std::string skeletonDataPath;

		std::shared_ptr<SpineSkeletonData> skeletonData;
		std::shared_ptr<renderer::Shader> shader;
		std::shared_ptr<renderer::Shader> occlusionShader;

		std::unique_ptr<spine::Skeleton> skeleton;
		std::unique_ptr<spine::AnimationStateData> animationStateData;
		std::unique_ptr<spine::AnimationState> animationState;

		renderer::Mesh dynamicMesh;

		bool isOccluder = false;
		bool onScreen = true;

		// buffers
		std::vector<renderer::SpineVertex> tempVerts;
		std::vector<uint16_t> tempIndices;

		// Cache last bound texture
		unsigned int lastBoundTexture = 0;

		mutable std::unordered_map<std::string, glm::vec2> boneLocalOverrides;
	} m;

#ifdef TOAST_EDITOR
	struct {
		// Editor-only: UI state for animation preview
		std::vector<std::string> animationNames;
		int selectedAnimation = -1;
		bool loopAnimation = false;
		bool playing = false;
	} debug;

	// Helper to populate animation names from current skeleton data
	void RefreshAnimationList();
#endif
};
