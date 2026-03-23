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
#include <limits>
#include <imgui.h>

namespace physics {

class ColliderRenderable : public renderer::IRenderable {
	struct {
		std::vector<glm::vec3> points;
		std::vector<uint16_t> indices;
		std::vector<renderer::SpineVertex> vertices;
		renderer::BoundingBox boundingBox;
		renderer::Mesh mesh;
		std::shared_ptr<renderer::Material> material;
		std::string material_path;
		editor::ResourceSlot material_slot { resource::ResourceType::MATERIAL };

		// Top layer feature
		bool showTop = false;
		float maxSlope = 45.0f; // in degrees
		float topHeight = 0.5f;
		renderer::Mesh topMesh;
		std::vector<renderer::SpineVertex> topVertices;
		std::vector<uint16_t> topIndices;
		std::shared_ptr<renderer::Material> topMaterial;
		std::string topMaterialPath;
		editor::ResourceSlot topMaterialSlot { resource::ResourceType::MATERIAL };
	} m;

public:
	void SendVertices(std::vector<glm::vec3>& points);

	void Load(json_t j, bool force_create = true) override {
		PROFILE_ZONE_C(0x00FFFF);    // Cyan for deserialization
		if (j.contains("material_path")) {
			m.material_path = j.at("material_path");
		}
		if (j.contains("showTop")) {
			m.showTop = j.at("showTop");
		}
		if (j.contains("maxSlope")) {
			m.maxSlope = j.at("maxSlope");
		}
		if (j.contains("topHeight")) {
			m.topHeight = j.at("topHeight");
		}
		if (j.contains("topMaterialPath")) {
			m.topMaterialPath = j.at("topMaterialPath");
		}
	}

	[[nodiscard]]
	json_t Save() const override {
		json_t j;
		j["material_path"] = m.material_path;
		j["showTop"] = m.showTop;
		j["maxSlope"] = m.maxSlope;
		j["topHeight"] = m.topHeight;
		j["topMaterialPath"] = m.topMaterialPath;
		return j;
	}

	void Init() override {
		TransformComponent::Init();
		// init just for loading
		m.material = resource::LoadResource<renderer::Material>(m.material_path);
		m.topMaterial = resource::LoadResource<renderer::Material>(m.topMaterialPath);

		SetRunTick(false);
		SetRunEarlyTick(false);
		SetRunLateTick(false);

#ifdef TOAST_EDITOR
		m.material_slot.name("Material");
		m.material_slot.SetOnDroppedLambda([this](const std::string& p) {
			m.material_path = p;
			m.material = resource::LoadResource<renderer::Material>(p);
		});
		m.material_slot.SetInitialResource(m.material_path);

		m.topMaterialSlot.name("Top Material");
		m.topMaterialSlot.SetOnDroppedLambda([this](const std::string& p) {
			m.topMaterialPath = p;
			m.topMaterial = resource::LoadResource<renderer::Material>(p);
		});
		m.topMaterialSlot.SetInitialResource(m.topMaterialPath);
#endif
	}

	void Inspector() override {
		m.material_slot.Show();
		ImGui::Separator();
		ImGui::Checkbox("Show Top Layer", &m.showTop);
		if (m.showTop) {
			ImGui::DragFloat("Max Slope (Deg)", &m.maxSlope, 0.5f, 0.0f, 90.0f);
			ImGui::DragFloat("Top Height", &m.topHeight, 0.05f, 0.0f, 10.0f);
			m.topMaterialSlot.Show();
		}
	}

	void LoadTextures() override {
		m.mesh.InitDynamicSpine();
		m.mesh.UpdateDynamicSpine(m.vertices.data(), m.vertices.size(), m.indices.data(), m.indices.size());

		m.topMesh.InitDynamicSpine();
		m.topMesh.UpdateDynamicSpine(m.topVertices.data(), m.topVertices.size(), m.topIndices.data(), m.topIndices.size());
	}

	void OnRender(const glm::mat4& viewProjection) noexcept override {
		if (not enabled()) return;

		if (not OclussionVolume::isTransformedAABBOnPlanes(m.boundingBox, GetWorldMatrix())) {
			return;
		}

		PROFILE_ZONE;

		// compute transform once
		const glm::mat4 model = GetWorldMatrix();
		const glm::mat4 mvp = viewProjection * model;

		if (m.material && m.material->GetShader()) {
			m.material->Use();
			auto shader = m.material->GetShader();
			// upload world matrix for deferred / lighting passes
			shader->Set("gWorld", model);
			// set generic transform uniform
			shader->Set("gMVP", mvp);
			// draw
			m.mesh.DrawDynamicSpine(m.indices.size());
		}

		if (m.showTop && m.topMaterial && m.topMaterial->GetShader()) {
			m.topMaterial->Use();
			auto shader = m.topMaterial->GetShader();
			shader->Set("gWorld", model);
			shader->Set("gMVP", mvp);
			m.topMesh.DrawDynamicSpine(m.topIndices.size());
		}
	}

private:
	void CalculateBoundingBox() {
		float y_min = std::numeric_limits<float>::max();
		float y_max = std::numeric_limits<float>::lowest();
		float x_min = std::numeric_limits<float>::max();
		float x_max = std::numeric_limits<float>::lowest();

		for (auto& p : m.points) {
			y_min = std::min(p.y, y_min);
			y_max = std::max(p.y, y_max);
			x_min = std::min(p.x, x_min);
			x_max = std::max(p.x, x_max);
		}

		m.boundingBox = {
			.min = {x_min, y_min, 0.0f},
			.max = {x_max, y_max, 0.0f}
		};
	}
};

}
