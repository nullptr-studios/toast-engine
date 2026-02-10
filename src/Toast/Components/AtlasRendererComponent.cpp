/// @file AtlasRendererComponent.cpp
/// @author dario
/// @date 21/11/2025.

#include "Toast/Components/AtlasRendererComponent.hpp"

#include "Toast/Components/AtlasSpriteComponent.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Resources/Texture.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

void AtlasRendererComponent::Init() {
	TransformComponent::Init();

	m_shader = resource::LoadResource<renderer::Shader>("shaders/spine_atlas.shader");

	// Reserve temp buffers to avoid allocations
	m_tempVerts.reserve(INITIAL_VERT_RESERVE);
	m_tempIndices.reserve(INITIAL_VERT_RESERVE);

	if (!m_atlasPath.empty()) {
		m_atlas = resource::LoadResource<SpineAtlas>(m_atlasPath);
		if (m_atlas && m_atlas->GetResourceState() == resource::ResourceState::LOADEDCPU) {
			EnumerateRegionNames();
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

void AtlasRendererComponent::BuildQuadFromRegion(
    spine::AtlasRegion* region, const glm::mat4& transform, uint32_t color, std::vector<renderer::SpineVertex>& vertices,
    std::vector<uint16_t>& indices
) {
	if (!region) {
		return;
	}

	// Get UV coordinates from the region
	float u = region->u;
	float v = region->v;
	float u2 = region->u2;
	float v2 = region->v2;

	// Flip V coordinates on Y axis
	v = 1.0f - v;
	v2 = 1.0f - v2;

	// Get dimensions
	float width = static_cast<float>(region->width) / 50.0f;    // 1 scale unit is 50 pixls
	float height = static_cast<float>(region->height) / 50.0f;

	// Current vertex offset for indices
	uint16_t baseIndex = static_cast<uint16_t>(vertices.size());

	// 4 vertices for the quad
	std::array<renderer::SpineVertex, 4> quadVerts;

	// Handle rotated regions
	if (region->degrees == 90) {
		std::swap(width, height);

		float halfW = width * 0.5f;
		float halfH = height * 0.5f;

		// Bottom-left vertex
		quadVerts[0].position = glm::vec3(-halfW, -halfH, 0.0f);
		quadVerts[0].texCoord = glm::vec2(u2, v);
		quadVerts[0].colorABGR = color;

		// Bottom-right vertex
		quadVerts[1].position = glm::vec3(halfW, -halfH, 0.0f);
		quadVerts[1].texCoord = glm::vec2(u, v);
		quadVerts[1].colorABGR = color;

		// Top-right vertex
		quadVerts[2].position = glm::vec3(halfW, halfH, 0.0f);
		quadVerts[2].texCoord = glm::vec2(u, v2);
		quadVerts[2].colorABGR = color;

		// Top-left vertex
		quadVerts[3].position = glm::vec3(-halfW, halfH, 0.0f);
		quadVerts[3].texCoord = glm::vec2(u2, v2);
		quadVerts[3].colorABGR = color;
	} else {
		// Unrotated region - standard quad with Y-flipped UVs
		float halfW = width * 0.5f;
		float halfH = height * 0.5f;

		// Bottom-left
		quadVerts[0].position = glm::vec3(-halfW, -halfH, 0.0f);
		quadVerts[0].texCoord = glm::vec2(u, v2);
		quadVerts[0].colorABGR = color;

		// Bottom-right
		quadVerts[1].position = glm::vec3(halfW, -halfH, 0.0f);
		quadVerts[1].texCoord = glm::vec2(u2, v2);
		quadVerts[1].colorABGR = color;

		// Top-right
		quadVerts[2].position = glm::vec3(halfW, halfH, 0.0f);
		quadVerts[2].texCoord = glm::vec2(u2, v);
		quadVerts[2].colorABGR = color;

		// Top-left
		quadVerts[3].position = glm::vec3(-halfW, halfH, 0.0f);
		quadVerts[3].texCoord = glm::vec2(u, v);
		quadVerts[3].colorABGR = color;
	}

	// Apply rotation offset for rotated regions
	glm::mat4 finalTransform = transform;
	if (region->degrees == 90) {
		// Create a 90 degree rotation matrix
		glm::mat4 rotationOffset = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		finalTransform = transform * rotationOffset;
	}

	// Transform vertices by the sprite's transform
	for (auto& vert : quadVerts) {
		glm::vec4 transformedPos = finalTransform * glm::vec4(vert.position, 1.0f);
		vert.position = glm::vec3(transformedPos);
		vertices.push_back(vert);
	}

	// 6 indices for 2 triangles
	indices.push_back(baseIndex + 0);
	indices.push_back(baseIndex + 1);
	indices.push_back(baseIndex + 2);
	indices.push_back(baseIndex + 0);
	indices.push_back(baseIndex + 2);
	indices.push_back(baseIndex + 3);
}

void AtlasRendererComponent::RefreshSprites() {
	m_spriteCache.clear();

	// Collect all AtlasSpriteComponent children of this component
	for (auto& [id, childPtr] : children.GetAll()) {
		if (auto* sprite = dynamic_cast<toast::AtlasSpriteComponent*>(childPtr.get())) {
			// Update the sprite's region if needed
			if (sprite->GetRegion() == nullptr && !sprite->GetRegionName().empty() && m_atlas) {
				spine::AtlasRegion* region = FindRegion(sprite->GetRegionName());
				sprite->SetRegion(region);
			}
			m_spriteCache.push_back(sprite);
		}
	}

	m_spriteCacheDirty = false;
}

spine::AtlasRegion* AtlasRendererComponent::FindRegion(const std::string& regionName) const {
	if (!m_atlas || !m_atlas->GetAtlasData()) {
		return nullptr;
	}
	return m_atlas->GetAtlasData()->findRegion(regionName.c_str());
}

std::string AtlasRendererComponent::GenerateSpriteName(const std::string& regionName) {
	// Count existing sprites with this region name
	int count = 0;
	for (const auto& [id, childPtr] : children.GetAll()) {
		if (auto* sprite = dynamic_cast<toast::AtlasSpriteComponent*>(childPtr.get())) {
			if (sprite->GetRegionName() == regionName) {
				count++;
			}
		}
	}

	// Generate name
	return regionName + "_" + std::to_string(count);
}

void AtlasRendererComponent::OnRender(const glm::mat4& precomputed_mat) noexcept {
	if (!enabled()) {
		return;
	}

	if (!m_atlas || !m_atlas->GetAtlasData()) {
		return;
	}

	// Refresh sprite cache
	if (m_spriteCacheDirty) {
		RefreshSprites();
	}

	// Early exit if no sprites
	if (m_spriteCache.empty()) {
		return;
	}

	PROFILE_ZONE;

	const glm::mat4 parentWorld = GetWorldMatrix();

	// Clear buffers for this frame
	m_tempVerts.clear();
	m_tempIndices.clear();

	// Build geometry for all sprites
	for (auto* sprite : m_spriteCache) {
		if (!sprite->enabled() || !sprite->GetRegion()) {
			continue;
		}

		// Get sprite's world transform (relative to parent)
		glm::mat4 spriteTransform = parentWorld * sprite->GetMatrix();

		// Build quad for this sprite
		BuildQuadFromRegion(sprite->GetRegion(), spriteTransform, sprite->GetColorABGR(), m_tempVerts, m_tempIndices);
	}

	// Early exit if no visible sprites
	if (m_tempIndices.empty()) {
		return;
	}

	// Update mesh
	m_dynamicMesh.UpdateDynamicSpine(m_tempVerts.data(), m_tempVerts.size(), m_tempIndices.data(), m_tempIndices.size());

	// Compute bounding box
	m_dynamicMesh.ComputeSpineBoundingBox(m_tempVerts.data(), m_tempVerts.size());

	// Frustum culling
	const auto& frustumPlanes = renderer::IRendererBase::GetInstance()->GetFrustumPlanes();
	if (!OclussionVolume::isTransformedAABBOnPlanes(frustumPlanes, m_dynamicMesh.dynamicBoundingBox(), glm::mat4(1.0f))) {
		return;
	}

	const glm::mat4 mvp = precomputed_mat;

	m_shader->Use();
	m_shader->Set("transform", mvp);

	spine::Vector<spine::AtlasPage*>& pages = m_atlas->GetAtlasData()->getPages();
	if (pages.size() > 0) {
		std::shared_ptr<Texture>* tex_ptr = static_cast<std::shared_ptr<Texture>*>(pages[0]->texture);
		if (tex_ptr) {
			tex_ptr->get()->Bind(0);
		}
	}

	// Draw all sprites in one call
	m_dynamicMesh.DrawDynamicSpine(m_tempIndices.size());
}

void AtlasRendererComponent::LoadTextures() {
	m_shader->Use();
	m_shader->SetSampler("Texture", 0);

	renderer::IRendererBase::GetInstance()->AddRenderable(this);

	m_dynamicMesh.InitDynamicSpine();
}

void AtlasRendererComponent::Load(json_t j, bool force_create) {
	// Load atlas path first
	if (j.contains("atlasResourcePath")) {
		m_atlasPath = j.at("atlasResourcePath").get<std::string>();
	}

	TransformComponent::Load(j, false);

	// Mark cache as dirty so sprites get refreshed with proper regions on next render
	m_spriteCacheDirty = true;
}

void AtlasRendererComponent::UpdateMeshBounds() {
	if (!m_tempVerts.empty()) {
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
		m_spriteCacheDirty = true;
	}

	ImGui::Separator();

	if (!m_atlas || !m_atlas->GetAtlasData()) {
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "No atlas loaded");
		return;
	}

	ImGui::Text("Atlas: %s", m_atlasPath.c_str());
	ImGui::Text("Regions: %d", static_cast<int>(m_regionNames.size()));

	ImGui::Separator();
	ImGui::Text("Sprites: %zu", m_spriteCache.size());

	// Button to add new sprite
	if (ImGui::Button("Add Sprite")) {
		if (!m_regionNames.empty()) {
			// Use first region as default
			std::string spriteName = GenerateSpriteName(m_regionNames[0]);
			auto* sprite = children.Add<toast::AtlasSpriteComponent>(spriteName);
			if (sprite) {
				sprite->SetRegionName(m_regionNames[0]);
				sprite->SetRegion(FindRegion(m_regionNames[0]));
			}
			m_spriteCacheDirty = true;
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Refresh Sprites")) {
		m_spriteCacheDirty = true;
	}

	// Region picker for adding sprites
	if (!m_regionNames.empty()) {
		ImGui::Separator();
		ImGui::Text("Quick Add Region:");
		const char* preview = (m_selectedRegion >= 0 && m_selectedRegion < static_cast<int>(m_regionNames.size()))
		                          ? m_regionNames[m_selectedRegion].c_str()
		                          : "<select region>";
		if (ImGui::BeginCombo("##RegionCombo", preview)) {
			for (int i = 0; i < static_cast<int>(m_regionNames.size()); ++i) {
				const bool selected = (m_selectedRegion == i);
				if (ImGui::Selectable(m_regionNames[i].c_str(), selected)) {
					m_selectedRegion = i;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::Button("Add This Region") && m_selectedRegion >= 0) {
			std::string regionName = m_regionNames[m_selectedRegion];
			std::string spriteName = GenerateSpriteName(regionName);
			auto* sprite = children.Add<toast::AtlasSpriteComponent>(spriteName);
			if (sprite) {
				sprite->SetRegionName(regionName);
				sprite->SetRegion(FindRegion(regionName));
			}
			m_spriteCacheDirty = true;
		}
	}
}

#endif

json_t AtlasRendererComponent::Save() const {
	json_t j = TransformComponent::Save();
	j["atlasResourcePath"] = m_atlasPath;
	return j;
}

void AtlasRendererComponent::Destroy() {
	renderer::IRendererBase::GetInstance()->RemoveRenderable(this);
}
