/// @file PostProcessVolume.hpp
/// @author dario
/// @date 06/04/2026.

#pragma once

#include "Toast/Objects/Actor.hpp"
#include "Toast/Renderer/IPostProcess.hpp"

#include <memory>
#include <vector>

/// Post-process volume that overrides the global stack while the active camera is inside this 2D AABB.
class PostProcessVolume : public toast::Actor {
public:
	REGISTER_TYPE(PostProcessVolume);

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

protected:
	void Init() override;
	void Tick() override;

#ifdef TOAST_EDITOR
	void EditorTick() override;
#endif

private:
	void RebuildRuntimeStack();
	void ApplyOverrideIfInside();
	[[nodiscard]]
	bool IsCameraInside2D() const;

#ifdef TOAST_EDITOR
	void DrawDebugBounds() const;
#endif

	int m_priority = 0;
	glm::vec2 m_halfExtents = glm::vec2(5.0f);
	bool m_enabledVolume = true;

	std::vector<std::unique_ptr<IPostProcess>> m_stack;
	std::vector<IPostProcess*> m_runtimeStack;
};