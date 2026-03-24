/**
 * @file ColliderRenderable.hpp
 * @author Xein
 * @date 16/03/26
 *
 * @brief [TODO: Brief description of the file's purpose]
 */

#pragma once
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/Renderer/Material.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Resources/IResource.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Resources/ResourceSlot.hpp"
#include "nlohmann/detail/value_t.hpp"

#include <Toast/Renderer/IRenderable.hpp>
#include <algorithm>
#include <glm/glm.hpp>
#include <imgui.h>
#include <limits>

namespace physics {

class ColliderRenderable : public renderer::IRenderable {
	struct {
		std::vector<glm::vec3> points;
		std::vector<uint16_t> indices;
		std::vector<renderer::SpineVertex> vertices;
		renderer::BoundingBox boundingBox;
		renderer::Mesh mesh;
		std::shared_ptr<renderer::Material> material;
		std::shared_ptr<renderer::Shader> occlusionShader;
		std::string material_path;
		editor::ResourceSlot material_slot { resource::ResourceType::MATERIAL };
		float horizontal_offset;

		// Top layer feature
		bool show = false;
		bool showTop = false;
		bool isOccluder = false;
		float maxSlope = 45.0f;    // in degrees
		float topHeight = 0.5f;
		float topOffset = 0.0f;
		renderer::Mesh topMesh;
		std::vector<renderer::SpineVertex> topVertices;
		std::vector<uint16_t> topIndices;
		std::shared_ptr<renderer::Material> topMaterial;
		std::string topMaterialPath;
		editor::ResourceSlot topMaterialSlot { resource::ResourceType::MATERIAL };
	} m;

public:
	void SendVertices(std::vector<glm::vec3>& points);

	void Load(json_t j, bool force_create = true) override;

	[[nodiscard]]
	json_t Save() const override;

	void Init() override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	void LoadTextures() override;

	void OnRender(renderer::IRenderablePass pass, const glm::mat4& view_projection) noexcept override;

private:
	void CalculateBoundingBox();
};

}
