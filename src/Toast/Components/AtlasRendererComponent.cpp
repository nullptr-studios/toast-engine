/// @file AtlasRendererComponent.cpp
/// @author dario
/// @date 21/11/2025.

#include "Toast/Components/AtlasRendererComponent.hpp"

#include "Toast/Components/AtlasSpriteComponent.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Resources/Texture.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

void AtlasRendererComponent::Destroy() {
	renderer::IRendererBase::GetInstance()->RemoveTransparentRenderable(this);
}

void AtlasRendererComponent::Init() {
	PROFILE_ZONE_C(0xFF6B00);    // Orange for initialization
	TransformComponent::Init();

	m.shader = resource::LoadResource<renderer::Shader>("SHADERS/spine_atlas.shader");

	// Reserve temp buffers to avoid allocations
	m.tempVerts.reserve(INITIAL_VERT_RESERVE);
	m.tempIndices.reserve(INITIAL_VERT_RESERVE);

	SetRunEarlyTick(false);
	SetRunLateTick(false);
	SetRunTick(false);

	if (!m.atlasPath.empty()) {
		m.atlas = resource::LoadResource<SpineAtlas>(m.atlasPath);
		if (m.atlas && m.atlas->GetResourceState() == resource::ResourceState::LOADEDCPU) {
			EnumerateRegionNames();
		}
#ifdef TOAST_EDITOR
		debug.atlasResource.SetInitialResource(m.atlasPath);
#endif
	}
}

void AtlasRendererComponent::OnRender(const glm::mat4& precomputed_mat) noexcept {
	if (!enabled()) {
		return;
	}

	if (!m.atlas || !m.atlas->GetAtlasData()) {
		return;
	}

	// Refresh sprite cache
	if (m.spriteCacheDirty) {
		RefreshSprites();
	}

	// Early exit if no sprites
	if (m.spriteCache.empty()) {
		return;
	}

	PROFILE_ZONE;

	const glm::mat4 parent_world = GetWorldMatrix();

	// Clear buffers for this frame
	m.tempVerts.clear();
	m.tempIndices.clear();

	// Build geometry for all sprites
	for (auto* sprite : m.spriteCache) {
		if (!sprite->enabled() || !sprite->GetRegion()) {
			continue;
		}

		// Get sprite's world transform (relative to parent)
		glm::mat4 sprite_transform = parent_world * sprite->GetMatrix();

		// Build quad for this sprite
		BuildQuadFromRegion(sprite->GetRegion(), sprite_transform, sprite->GetColorABGR(), m.tempVerts, m.tempIndices);
	}

	// Early exit if no visible sprites
	if (m.tempIndices.empty()) {
		return;
	}

	// Update mesh
	m.dynamicMesh.UpdateDynamicSpine(m.tempVerts.data(), m.tempVerts.size(), m.tempIndices.data(), m.tempIndices.size());

	// Compute bounding box
	m.dynamicMesh.ComputeSpineBoundingBox(m.tempVerts.data(), m.tempVerts.size());

	// Frustum culling
	if (!OclussionVolume::isTransformedAABBOnPlanes(m.dynamicMesh.dynamicBoundingBox(), glm::mat4(1.0f))) {
		return;
	}

	const glm::mat4 mvp = precomputed_mat;

	m.shader->Use();
	m.shader->Set("transform", mvp);

	spine::Vector<spine::AtlasPage*>& pages = m.atlas->GetAtlasData()->getPages();
	if (pages.size() > 0) {
		std::shared_ptr<Texture>* tex_ptr = static_cast<std::shared_ptr<Texture>*>(pages[0]->texture);
		if (tex_ptr) {
			tex_ptr->get()->Bind(0);
		}
	}

	// Draw all sprites in one call
	m.dynamicMesh.DrawDynamicSpine(m.tempIndices.size());
}

void AtlasRendererComponent::LoadTextures() {
	PROFILE_ZONE_C(0xFFFF00);    // Yellow for resource loading
	m.shader->Use();
	m.shader->SetSampler("Texture", 0);

	renderer::IRendererBase::GetInstance()->AddTransparentRenderable(this);

	m.dynamicMesh.InitDynamicSpine();
}

void AtlasRendererComponent::Load(json_t j, bool force_create) {
	// Load atlas path first
	if (j.contains("atlasResourcePath")) {
		m.atlasPath = j.at("atlasResourcePath").get<std::string>();
	}

	TransformComponent::Load(j, force_create);

	// Mark cache as dirty so sprites get refreshed with proper regions on next render
	m.spriteCacheDirty = true;
}

json_t AtlasRendererComponent::Save() const {
	json_t j = TransformComponent::Save();
	j["atlasResourcePath"] = m.atlasPath;
	return j;
}

void AtlasRendererComponent::EnumerateRegionNames() {
	m.regionNames.clear();

	if (!m.atlas || !m.atlas->GetAtlasData()) {
		return;
	}

	spine::Atlas* atlas = m.atlas->GetAtlasData();
	spine::Vector<spine::AtlasRegion*>& regions = atlas->getRegions();

	m.regionNames.reserve(regions.size());
	for (size_t i = 0; i < regions.size(); ++i) {
		m.regionNames.emplace_back(regions[i]->name.buffer());
	}
}

void AtlasRendererComponent::BuildQuadFromRegion(
    spine::AtlasRegion* region, const glm::mat4& transform, uint32_t color, std::vector<renderer::SpineVertex>& vertices,
    std::vector<uint16_t>& indices
) {
	if (!region) {
		return;
	}

	float width = static_cast<float>(region->width) / 50.0f;
	float height = static_cast<float>(region->height) / 50.0f;

	float half_w = width * 0.5f;
	float half_h = height * 0.5f;

	// Current vertex offset for indices
	uint16_t base_index = static_cast<uint16_t>(vertices.size());

	// 4 vertices for the quad
	std::array<renderer::SpineVertex, 4> quad_verts;

	// Quad positions
	// Bottom-left, Bottom-right, Top-right, Top-left
	quad_verts[0].position = glm::vec3(-half_w, -half_h, 0.0f);
	quad_verts[1].position = glm::vec3(half_w, -half_h, 0.0f);
	quad_verts[2].position = glm::vec3(half_w, half_h, 0.0f);
	quad_verts[3].position = glm::vec3(-half_w, half_h, 0.0f);

	// Set colors
	quad_verts[0].colorABGR = color;
	quad_verts[1].colorABGR = color;
	quad_verts[2].colorABGR = color;
	quad_verts[3].colorABGR = color;

	// Get UV coordinates from the region
	float u = region->u;
	float v = region->v;
	float u2 = region->u2;
	float v2 = region->v2;

	if (region->degrees == 90) {
		// Sprite is rotated 90° CW in the atlas
		// Rotate UVs to compensate
		quad_verts[0].texCoord = glm::vec2(u2, v2);    // BL
		quad_verts[1].texCoord = glm::vec2(u2, v);     // BR
		quad_verts[2].texCoord = glm::vec2(u, v);      // TR
		quad_verts[3].texCoord = glm::vec2(u, v2);     // TL
	} else {
		// Unrotated sprite - standard UV mapping
		quad_verts[0].texCoord = glm::vec2(u, v2);     // Bottom-left
		quad_verts[1].texCoord = glm::vec2(u2, v2);    // Bottom-right
		quad_verts[2].texCoord = glm::vec2(u2, v);     // Top-right
		quad_verts[3].texCoord = glm::vec2(u, v);      // Top-left
	}

	// Transform vertices by the sprite's transform
	for (auto& vert : quad_verts) {
		glm::vec4 transformed_pos = transform * glm::vec4(vert.position, 1.0f);
		vert.position = glm::vec3(transformed_pos);
		vertices.push_back(vert);
	}

	// 6 indices for 2 triangles
	indices.push_back(base_index + 0);
	indices.push_back(base_index + 1);
	indices.push_back(base_index + 2);
	indices.push_back(base_index + 0);
	indices.push_back(base_index + 2);
	indices.push_back(base_index + 3);
}

void AtlasRendererComponent::RefreshSprites() {
	m.spriteCache.clear();

	// Collect all AtlasSpriteComponent children of this component
	for (auto& [id, childPtr] : children.GetAll()) {
		if (auto* sprite = dynamic_cast<toast::AtlasSpriteComponent*>(childPtr.get())) {
			// Update the sprite's region if needed
			if (sprite->GetRegion() == nullptr && !sprite->GetRegionName().empty() && m.atlas) {
				spine::AtlasRegion* region = FindRegion(sprite->GetRegionName());
				sprite->SetRegion(region);
			}
			m.spriteCache.push_back(sprite);
		}
	}

	m.spriteCacheDirty = false;
}

spine::AtlasRegion* AtlasRendererComponent::FindRegion(std::string_view region_name) const {
	if (!m.atlas || !m.atlas->GetAtlasData()) {
		return nullptr;
	}
	return m.atlas->GetAtlasData()->findRegion(region_name.data());
}

std::string AtlasRendererComponent::GenerateSpriteName(std::string_view region_name) {
	// Count existing sprites with this region name
	int count = 0;
	for (const auto& [id, childPtr] : children.GetAll()) {
		if (auto* sprite = dynamic_cast<toast::AtlasSpriteComponent*>(childPtr.get())) {
			if (sprite->GetRegionName() == region_name) {
				count++;
			}
		}
	}

	// Generate name
	return std::format("{}_{}", region_name, count);
}

void AtlasRendererComponent::UpdateMeshBounds() {
	if (!m.tempVerts.empty()) {
		m.dynamicMesh.ComputeSpineBoundingBox(m.tempVerts.data(), m.tempVerts.size());
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

	debug.atlasResource.Show();

	if (ImGui::Button("Load Atlas")) {
		if (debug.atlasResource.GetResourcePath().empty()) {
			TOAST_WARN("AtlasRendererComponent::Inspector() Cannot load atlas: path is empty");
			return;
		}

		m.atlasPath = debug.atlasResource.GetResourcePath();

		m.atlas = resource::LoadResource<SpineAtlas>(m.atlasPath);
		if (!m.atlas || m.atlas->GetResourceState() == resource::ResourceState::FAILED) {
			TOAST_ERROR("AtlasRendererComponent::Inspector() Failed loading SpineAtlas from path \"{0}\"", m.atlasPath);
			return;
		}

		// Enumerate regions
		EnumerateRegionNames();
		m.spriteCacheDirty = true;
	}

	ImGui::Separator();

	if (!m.atlas || !m.atlas->GetAtlasData()) {
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "No atlas loaded");
		return;
	}

	ImGui::Text("Atlas: %s", m.atlasPath.c_str());
	ImGui::Text("Regions: %d", static_cast<int>(m.regionNames.size()));

	ImGui::Separator();
	ImGui::Text("Sprites: %zu", m.spriteCache.size());

	// Button to add new sprite
	if (ImGui::Button("Add Sprite")) {
		if (!m.regionNames.empty()) {
			// Use first region as default
			std::string sprite_name = GenerateSpriteName(m.regionNames[0]);
			auto* sprite = children.Add<toast::AtlasSpriteComponent>(sprite_name);
			if (sprite) {
				sprite->SetRegionName(m.regionNames[0]);
				sprite->SetRegion(FindRegion(m.regionNames[0]));
			}
			m.spriteCacheDirty = true;
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Refresh Sprites")) {
		m.spriteCacheDirty = true;
	}

	// Region picker for adding sprites
	if (!m.regionNames.empty()) {
		ImGui::Separator();
		ImGui::Text("Quick Add Region:");
		const char* preview = (m.selectedRegion >= 0 && m.selectedRegion < static_cast<int>(m.regionNames.size()))
		                          ? m.regionNames[m.selectedRegion].c_str()
		                          : "<select region>";
		if (ImGui::BeginCombo("##RegionCombo", preview)) {
			for (int i = 0; i < static_cast<int>(m.regionNames.size()); ++i) {
				const bool selected = (m.selectedRegion == i);
				if (ImGui::Selectable(m.regionNames[i].c_str(), selected)) {
					m.selectedRegion = i;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::Button("Add This Region") && m.selectedRegion >= 0) {
			std::string region_name = m.regionNames[m.selectedRegion];
			std::string sprite_name = GenerateSpriteName(region_name);
			auto* sprite = children.Add<toast::AtlasSpriteComponent>(sprite_name);
			if (sprite) {
				sprite->SetRegionName(region_name);
				sprite->SetRegion(FindRegion(region_name));
			}
			m.spriteCacheDirty = true;
		}
	}
}

#endif
