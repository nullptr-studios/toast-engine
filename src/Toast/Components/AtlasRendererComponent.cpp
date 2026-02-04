/// @file AtlasRendererComponent.cpp
/// @author dario
/// @date 21/11/2025.

#include "Toast/Components/AtlasRendererComponent.hpp"

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Resources/Texture.hpp"

#include <algorithm>

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

void AtlasRendererComponent::Init() {
	m_shader = resource::LoadResource<renderer::Shader>("shaders/spine_atlas.shader");

	// Reserve temp buffers to avoid allocations
	m_tempVerts.reserve(INITIAL_VERT_RESERVE);
	m_tempIndices.reserve(INITIAL_VERT_RESERVE);

	if (!m_atlasPath.empty()) {
		m_atlas = resource::LoadResource<SpineAtlas>(m_atlasPath);
		if (m_atlas && m_atlas->GetResourceState() == resource::ResourceState::LOADEDCPU) {
			EnumerateRegionNames();
			
			// If we have a saved region name, select it
			if (!m_selectedRegionName.empty()) {
				auto it = std::find(m_regionNames.begin(), m_regionNames.end(), m_selectedRegionName);
				if (it != m_regionNames.end()) {
					m_selectedRegion = static_cast<int>(std::distance(m_regionNames.begin(), it));
					m_currentRegion = m_atlas->GetAtlasData()->findRegion(m_selectedRegionName.c_str());
					if (m_currentRegion) {
						BuildQuadFromRegion(m_currentRegion);
					}
				}
			}
		}
		
#ifdef TOAST_EDITOR
		m_atlasResource.SetInitialResource(m_atlasPath);
#endif
	}
}

void AtlasRendererComponent::EnumerateRegionNames() {
	m_regionNames.clear();
	
	if (!m_atlas || !m_atlas->GetAtlasData()) {
		return;
	}
	
	spine::Atlas* atlas = m_atlas->GetAtlasData();
	spine::Vector<spine::AtlasRegion*>& regions = atlas->getRegions();
	
	m_regionNames.reserve(regions.size());
	for (size_t i = 0; i < regions.size(); ++i) {
		m_regionNames.push_back(regions[i]->name.buffer());
	}
}

void AtlasRendererComponent::BuildQuadFromRegion(spine::AtlasRegion* region) {
	if (!region) {
		return;
	}
	
	m_tempVerts.clear();
	m_tempIndices.clear();
	
	// Get UV coordinates from the region
	float u = region->u;
	float v = region->v;
	float u2 = region->u2;
	float v2 = region->v2;
	
	// Flip V coordinates on Y axis
	v = 1.0f - v;
	v2 = 1.0f - v2;
	
	// Get dimensions
	float width = static_cast<float>(region->width) / 50.0f;   // Spine uses pixels, convert to world units (assuming 50 pixels = 1 unit)
	float height = static_cast<float>(region->height) / 50.0f;
	
	// Handle rotated regions
	if (region->degrees == 90) {
		// Swap dimensions
		std::swap(width, height);
		
		// Rotate UVs: swap U and V
		float temp_u = u;
		float temp_u2 = u2;
		float temp_v = v;
		float temp_v2 = v2;
		
		u = temp_v;
		u2 = temp_v2;
		v = temp_u2;
		v2 = temp_u;
	}
	
	// Build a quad centered at origin
	float halfW = width * 0.5f;
	float halfH = height * 0.5f;
	
	// Default white color (ABGR format)
	uint32_t color = 0xFFFFFFFF;
	
	// 4 vertices for the quad
	m_tempVerts.resize(4);
	
	// Bottom-left
	m_tempVerts[0].position = glm::vec3(-halfW, -halfH, 0.0f);
	m_tempVerts[0].texCoord = glm::vec2(u, v2);
	m_tempVerts[0].colorABGR = color;
	
	// Bottom-right
	m_tempVerts[1].position = glm::vec3(halfW, -halfH, 0.0f);
	m_tempVerts[1].texCoord = glm::vec2(u2, v2);
	m_tempVerts[1].colorABGR = color;
	
	// Top-right
	m_tempVerts[2].position = glm::vec3(halfW, halfH, 0.0f);
	m_tempVerts[2].texCoord = glm::vec2(u2, v);
	m_tempVerts[2].colorABGR = color;
	
	// Top-left
	m_tempVerts[3].position = glm::vec3(-halfW, halfH, 0.0f);
	m_tempVerts[3].texCoord = glm::vec2(u, v);
	m_tempVerts[3].colorABGR = color;
	
	// 6 indices for 2 triangles
	m_tempIndices = { 0, 1, 2, 0, 2, 3 };
	
	// Update dynamic mesh
	m_dynamicMesh.UpdateDynamicSpine(m_tempVerts.data(), m_tempVerts.size(), m_tempIndices.data(), m_tempIndices.size());
	
	// Compute bounding box
	m_dynamicMesh.ComputeSpineBoundingBox(m_tempVerts.data(), m_tempVerts.size());
}

void AtlasRendererComponent::OnRender(const glm::mat4& precomputed_mat) noexcept {
	if (!enabled()) {
		return;
	}

	if (!m_currentRegion || !m_atlas) {
		return;
	}

	const glm::mat4 model = GetWorldMatrix();

	// Frustum culling using the dynamic AABB
	const auto& frustumPlanes = renderer::IRendererBase::GetInstance()->GetFrustumPlanes();
	if (!OclussionVolume::isTransformedAABBOnPlanes(frustumPlanes, m_dynamicMesh.dynamicBoundingBox(), model)) {
		return;    // Outside frustum, skip rendering
	}

	PROFILE_ZONE;

	const glm::mat4 mvp = precomputed_mat * model;

	m_shader->Use();
	m_shader->Set("transform", mvp);

	// Bind the texture from the atlas page
	std::shared_ptr<Texture>* tex_ptr = static_cast<std::shared_ptr<Texture>*>(m_currentRegion->page->texture);
	if (tex_ptr && tex_ptr->get()) {
		tex_ptr->get()->Bind(0);
	}

	// Draw the quad
	m_dynamicMesh.DrawDynamicSpine(m_tempIndices.size());
}

void AtlasRendererComponent::LoadTextures() {
	m_shader->Use();
	m_shader->SetSampler("Texture", 0);

	renderer::IRendererBase::GetInstance()->AddRenderable(this);

	m_dynamicMesh.InitDynamicSpine();
}

void AtlasRendererComponent::Load(json_t j, bool force_create) {
	TransformComponent::Load(j, force_create);
	
	if (j.contains("atlasResourcePath")) {
		m_atlasPath = j.at("atlasResourcePath").get<std::string>();
	}
	if (j.contains("selectedRegion")) {
		m_selectedRegionName = j.at("selectedRegion").get<std::string>();
	}
}

void AtlasRendererComponent::UpdateMeshBounds() {
	if (m_currentRegion) {
		m_dynamicMesh.ComputeSpineBoundingBox(m_tempVerts.data(), m_tempVerts.size());
	}
}

#ifdef TOAST_EDITOR

void AtlasRendererComponent::Inspector() {
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(20);
		TransformComponent::Inspector();
		ImGui::Unindent(20);
	}
	ImGui::Spacing();

	m_atlasResource.Show();

	if (ImGui::Button("Load Atlas")) {
		if (m_atlasResource.GetResourcePath().empty()) {
			TOAST_WARN("AtlasRendererComponent::Inspector() Cannot load atlas: path is empty");
			return;
		}

		m_atlasPath = m_atlasResource.GetResourcePath();

		m_atlas = resource::LoadResource<SpineAtlas>(m_atlasPath);
		if (!m_atlas || m_atlas->GetResourceState() == resource::ResourceState::FAILED) {
			TOAST_ERROR("AtlasRendererComponent::Inspector() Failed loading SpineAtlas from path \"{0}\"", m_atlasPath);
			return;
		}

		// Enumerate regions
		EnumerateRegionNames();

		// Apply saved selection if present
		if (!m_selectedRegionName.empty() && !m_regionNames.empty()) {
			auto it = std::find(m_regionNames.begin(), m_regionNames.end(), m_selectedRegionName);
			if (it != m_regionNames.end()) {
				m_selectedRegion = static_cast<int>(std::distance(m_regionNames.begin(), it));
				m_currentRegion = m_atlas->GetAtlasData()->findRegion(m_selectedRegionName.c_str());
				if (m_currentRegion) {
					BuildQuadFromRegion(m_currentRegion);
				}
			}
		}
	}

	ImGui::Separator();

	if (!m_atlas || !m_atlas->GetAtlasData()) {
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "No atlas loaded");
		return;
	}

	ImGui::Text("Regions: %d", static_cast<int>(m_regionNames.size()));
	
	// Region picker
	if (!m_regionNames.empty()) {
		const char* preview = (m_selectedRegion >= 0 && m_selectedRegion < static_cast<int>(m_regionNames.size()))
		                          ? m_regionNames[m_selectedRegion].c_str()
		                          : "<none>";
		if (ImGui::BeginCombo("Region", preview)) {
			for (int i = 0; i < static_cast<int>(m_regionNames.size()); ++i) {
				const bool selected = (m_selectedRegion == i);
				if (ImGui::Selectable(m_regionNames[i].c_str(), selected)) {
					m_selectedRegion = i;
					m_selectedRegionName = m_regionNames[i];
					m_currentRegion = m_atlas->GetAtlasData()->findRegion(m_selectedRegionName.c_str());
					if (m_currentRegion) {
						BuildQuadFromRegion(m_currentRegion);
					}
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
	
	// Show current region info
	if (m_currentRegion) {
		ImGui::Separator();
		ImGui::Text("Region Info:");
		ImGui::Text("  Size: %d x %d", m_currentRegion->width, m_currentRegion->height);
		ImGui::Text("  UV: (%.3f, %.3f) - (%.3f, %.3f)", 
			m_currentRegion->u, m_currentRegion->v, 
			m_currentRegion->u2, m_currentRegion->v2);
		if (m_currentRegion->degrees != 0) {
			ImGui::Text("  Rotation: %d degrees", m_currentRegion->degrees);
		}
	}
}

#endif

json_t AtlasRendererComponent::Save() const {
	json_t j = TransformComponent::Save();
	j["atlasResourcePath"] = m_atlasPath;
	j["selectedRegion"] = m_selectedRegionName;
	return j;
}

void AtlasRendererComponent::Destroy() {
	renderer::IRendererBase::GetInstance()->RemoveRenderable(this);
}
