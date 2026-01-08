/// @file AtlasRendererComponent.hpp
/// @author dario
/// @date 21/11/2025.

#pragma once
#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceSlot.hpp"
#include "Toast/Resources/Spine/SpineSkeletonData.hpp"
#include "spine/Skeleton.h"

#include <memory>
#include <string>
#include <vector>

class AtlasRendererComponent : public IRenderable {
public:
	REGISTER_TYPE(AtlasRendererComponent);

	void Init() override;

	void OnRender(const glm::mat4&) noexcept override;

	void LoadTextures() override;

	void Load(json_t j, bool force_create = true) override;

	json_t Save() const override;

	void Destroy() override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

private:
	// Editor resource slots
	editor::ResourceSlot m_atlasResource { resource::ResourceType::SPINE_ATLAS };
	editor::ResourceSlot m_skeletonDataResource { resource::ResourceType::SPINE_SKELETON_DATA };

	std::string m_atlasPath;
	std::string m_skeletonDataPath;
	std::string m_atlasObject;

	std::shared_ptr<SpineSkeletonData> m_skeletonData;
	std::shared_ptr<renderer::Shader> m_shader;

	std::unique_ptr<spine::Skeleton> m_skeleton;

	renderer::Mesh m_dynamicMesh;

	// reuse buffers to avoid per-frame allocations
	std::vector<renderer::SpineVertex> m_tempVerts;
	std::vector<uint16_t> m_tempIndices;

	// Cache last bound texture
	unsigned int m_lastBoundTexture = 0;
	static constexpr size_t INITIAL_VERT_RESERVE = 1024;

	// Attachment picker
	std::vector<std::string> m_attachmentNames;
	int m_selectedAttachment = -1;

	void EnumerateAttachmentNames();
	void SetOnlyAttachmentByName(const std::string& name) const;
};
