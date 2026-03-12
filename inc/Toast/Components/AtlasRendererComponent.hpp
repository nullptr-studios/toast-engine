/// @file AtlasRendererComponent.hpp
/// @author dario
/// @date 21/11/2025.

#pragma once
#include "Toast/Components/AtlasSpriteComponent.hpp"
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

	void OnEnable() override;
	void OnDisable() override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	// Sprite management
	void RefreshSprites();
	void RemoveSpriteFromCache(toast::AtlasSpriteComponent* sprite);
	void AddSpriteToCache(toast::AtlasSpriteComponent* sprite);
	spine::AtlasRegion* FindRegion(std::string_view region_name) const;
	std::string GenerateSpriteName(std::string_view region_name);

private:
	void UpdateMeshBounds();
	void EnumerateRegionNames();
	void BuildQuadFromRegion(
	    spine::AtlasRegion* region, const glm::mat4& transform, uint32_t color, std::vector<renderer::SpineVertex>& vertices,
	    std::vector<uint16_t>& indices
	);
	static constexpr size_t INITIAL_VERT_RESERVE = 256;

	struct {
		std::string atlasPath;

		std::shared_ptr<SpineAtlas> atlas;
		std::shared_ptr<renderer::Shader> shader;

		renderer::Mesh dynamicMesh;

		// Buffers for instanced rendering (all sprites combined)
		std::vector<renderer::SpineVertex> tempVerts;
		std::vector<uint16_t> tempIndices;

		// Cache last bound texture
		unsigned int lastBoundTexture = 0;

		// Region picker (for editor)
		std::vector<std::string> regionNames;
		int selectedRegion = -1;

		std::vector<toast::AtlasSpriteComponent*> spriteCache;
		bool spriteCacheDirty = true;
	} m;

#ifdef TOAST_EDITOR
	struct {
		editor::ResourceSlot atlasResource { resource::ResourceType::SPINE_ATLAS };
	} debug;
#endif
};
