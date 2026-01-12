/// @file SpineRendererComponent.hpp
/// @author dario
/// @date 23/10/2025.

#pragma once
#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceSlot.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Spine/SpineSkeletonData.hpp"
#include "spine/AnimationState.h"
#include "spine/AnimationStateData.h"
#include "spine/Skeleton.h"

#include "ResourceManager/Spine/SpineEventHandler.hpp"

class SpineRendererComponent : public IRenderable {
public:
	REGISTER_TYPE(SpineRendererComponent);

	void Init() override;

	void LoadTextures() override;

	void Begin() override;
	void Tick() override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	void Destroy() override;

	void OnRender(const glm::mat4&) noexcept override;

	void SetSkeletonData(const std::shared_ptr<SpineSkeletonData>& data) {
		m_skeletonData = data;
	}

	void Load(json_t j, bool force_create = true) override;
	json_t Save() const override;

	void PlayAnimation(const std::string_view& name, bool loop, int track = 0) const;
	void StopAnimation(int track = 0) const;

	void NextCrossFadeToDefault(float duration, int track = 0) const;
	void CrossFadeToDefault(float duration, int track = 0) const;

	glm::vec2 GetBoneLocalPosition(const std::string_view& boneName) const;
	void SetBoneLocalPosition(const std::string_view& boneName, const glm::vec2& position) const;
	
	
	// Events
	virtual void OnAnimationStart(const std::string_view& animationName, int track) {}
	virtual void OnAnimationCompleted(const std::string_view& animationName, int track) {}
	virtual void OnAnimationEnd(const std::string_view& animationName, int track) {}
	virtual void OnAnimationInterrupted(const std::string_view& animationName, int track) {}
	virtual void OnAnimationDispose(const std::string_view& animationName, int track) {}
	virtual void OnAnimationEvent(const std::string_view& animationName, int track, const std::string_view& eventName) {}
	

private:
	
	void HandleSpineEvents(spine::AnimationState* state, spine::EventType type, spine::TrackEntry* entry, spine::Event* event);
	
	std::unique_ptr<SpineEventHandler> m_eventHandler;
	
	editor::ResourceSlot m_atlasResource { resource::ResourceType::SPINE_ATLAS };
	editor::ResourceSlot m_skeletonDataResource { resource::ResourceType::SPINE_SKELETON_DATA };

	// Persisted resource paths (mirrors AtlasRendererComponent)
	std::string m_atlasPath;
	std::string m_skeletonDataPath;

	std::shared_ptr<SpineSkeletonData> m_skeletonData;
	std::shared_ptr<renderer::Shader> m_shader;

	std::unique_ptr<spine::Skeleton> m_skeleton;
	std::unique_ptr<spine::AnimationStateData> m_animationStateData;
	std::unique_ptr<spine::AnimationState> m_animationState;

	renderer::Mesh m_dynamicMesh;

	// buffers
	std::vector<renderer::SpineVertex> m_tempVerts;
	std::vector<uint16_t> m_tempIndices;

	// Cache last bound texture
	unsigned int m_lastBoundTexture = 0;
	static constexpr size_t INITIAL_VERT_RESERVE = 1024;

#ifdef TOAST_EDITOR
	// Editor-only: UI state for animation preview
	std::vector<std::string> m_animationNames;
	int m_selectedAnimation = -1;
	bool m_loopAnimation = false;
	bool m_playing = false;
	// Helper to populate animation names from current skeleton data
	void RefreshAnimationList();
#endif
};
