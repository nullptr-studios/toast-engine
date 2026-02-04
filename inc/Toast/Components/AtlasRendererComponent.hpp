/// @file AtlasRendererComponent.hpp
/// @author dario
/// @date 21/11/2025.

#pragma once
#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceSlot.hpp"
#include "Toast/Resources/Spine/SpineAtlas.hpp"
#include "spine/Atlas.h"

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
	void UpdateMeshBounds();
	void EnumerateRegionNames();
	void BuildQuadFromRegion(spine::AtlasRegion* region);

	// Editor resource slots
	editor::ResourceSlot m_atlasResource { resource::ResourceType::SPINE_ATLAS };

	std::string m_atlasPath;
	std::string m_selectedRegionName;

	std::shared_ptr<SpineAtlas> m_atlas;
	std::shared_ptr<renderer::Shader> m_shader;

	// Currently selected region
	spine::AtlasRegion* m_currentRegion = nullptr;

	renderer::Mesh m_dynamicMesh;

	// reuse buffers to avoid per-frame allocations
	std::vector<renderer::SpineVertex> m_tempVerts;
	std::vector<uint16_t> m_tempIndices;

	// Cache last bound texture
	unsigned int m_lastBoundTexture = 0;
	static constexpr size_t INITIAL_VERT_RESERVE = 64;

	// Region picker
	std::vector<std::string> m_regionNames;
	int m_selectedRegion = -1;
};
