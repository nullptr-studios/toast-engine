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
	} m;

public:
	void SendVertices(std::vector<glm::vec3>& points);

	void Load(json_t j, bool force_create = true) override {
		PROFILE_ZONE_C(0x00FFFF);    // Cyan for deserialization
		if (j.contains("material_path")) {
			m.material_path = j.at("material_path");
		}
	}

	[[nodiscard]]
	json_t Save() const override {
		json_t j;
		j["material_path"] = m.material_path;
		return j;
	}

	void Init() override {
		TransformComponent::Init();
		// init just for loading
		m.material = resource::LoadResource<renderer::Material>(m.material_path);

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
#endif
	}

	void Inspector() override {
		m.material_slot.Show();
	}

	void LoadTextures() override {
		m.mesh.InitDynamicSpine();
		m.mesh.UpdateDynamicSpine(m.vertices.data(), m.vertices.size(), m.indices.data(), m.indices.size());
	}

	void OnRender(const glm::mat4& viewProjection) noexcept override {
		if (not enabled()) return;

		if (not OclussionVolume::isTransformedAABBOnPlanes(m.boundingBox, GetWorldMatrix())) {
			return;
		}

		if (m.material == nullptr) return;
		if (m.material->GetShader() == nullptr) return;
		
		PROFILE_ZONE;

		// compute transform once
		const glm::mat4 model = GetWorldMatrix();
		const glm::mat4 mvp = viewProjection * model;

		m.material->Use();
		auto shader = m.material->GetShader();
		if (shader) {
			// upload world matrix for deferred / lighting passes
			shader->Set("gWorld", model);

			// set generic transform uniform
			shader->Set("gMVP", mvp);
		}

		// draw
		m.mesh.DrawDynamicSpine(m.indices.size());

		// restore state
		// m_texture->Unbind();
		// m_shader->unuse();

		// for (size_t i = 0; i < m.indices.size(); i += 3) {
		// 	renderer::DebugLine(m.points[m.indices[i]], m.points[m.indices[i+1]], glm::vec4{0.0, 1.0, 1.0, 1.0});
		// 	renderer::DebugLine(m.points[m.indices[i+1]], m.points[m.indices[i+2]], glm::vec4{0.0, 1.0, 1.0, 1.0});
		// 	renderer::DebugLine(m.points[m.indices[i+2]], m.points[m.indices[i]], glm::vec4{0.0, 1.0, 1.0, 1.0});
		// }
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
