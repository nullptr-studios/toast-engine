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

namespace toast {
class AtlasSpriteComponent;
}

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

	// Sprite management
	void RefreshSprites();
	spine::AtlasRegion* FindRegion(const std::string& regionName) const;
	std::string GenerateSpriteName(const std::string& regionName);

private:
	void UpdateMeshBounds();
	void EnumerateRegionNames();
	void BuildQuadFromRegion(
	    spine::AtlasRegion* region, const glm::mat4& transform, uint32_t color, std::vector<renderer::SpineVertex>& vertices,
	    std::vector<uint16_t>& indices
	);

	// Editor resource slots
	editor::ResourceSlot m_atlasResource { resource::ResourceType::SPINE_ATLAS };

	std::string m_atlasPath;

	std::shared_ptr<SpineAtlas> m_atlas;
	std::shared_ptr<renderer::Shader> m_shader;

	renderer::Mesh m_dynamicMesh;

	// Buffers for instanced rendering (all sprites combined)
	std::vector<renderer::SpineVertex> m_tempVerts;
	std::vector<uint16_t> m_tempIndices;

	// Cache last bound texture
	unsigned int m_lastBoundTexture = 0;
	static constexpr size_t INITIAL_VERT_RESERVE = 256;

	// Region picker (for editor)
	std::vector<std::string> m_regionNames;
	int m_selectedRegion = -1;

	// Cache sprite children for faster access
	std::vector<toast::AtlasSpriteComponent*> m_spriteCache;
	bool m_spriteCacheDirty = true;
};
