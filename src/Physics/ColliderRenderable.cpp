#include "Toast/Physics/ColliderRenderable.hpp"

#include "Toast/Log.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#ifdef TOAST_EDITOR
#include <imgui.h>
#endif
#include <vector>

using namespace glm;

static bool isPointInTriangle(const vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
	vec2 ab = b - a;
	vec2 bc = c - b;
	vec2 ca = a - c;

	vec2 ap = p - a;
	vec2 bp = p - b;
	vec2 cp = p - c;

	float cross1 = determinant(glm::mat2 { ab, ap });
	float cross2 = determinant(glm::mat2 { bc, bp });
	float cross3 = determinant(glm::mat2 { ca, cp });

	bool has_neg = (cross1 < 0.0f) || (cross2 < 0.0f) || (cross3 < 0.0f);
	bool has_pos = (cross1 > 0.0f) || (cross2 > 0.0f) || (cross3 > 0.0f);

	return !(has_neg && has_pos);
}

static std::vector<uint16_t> triangulate(const std::vector<vec3>& vertices) {
	std::vector<uint16_t> indices;

	// A polygon must have at least 3 vertices
	if (vertices.size() < 3) {
		return indices;
	}

	// Create a working list of indices representing the remaining polygon
	std::vector<size_t> remaining_indices(vertices.size());
	for (size_t i = 0; i < vertices.size(); ++i) {
		remaining_indices[i] = i;
	}

	// winding order
	float area = 0.0f;
	for (size_t i = 0; i < vertices.size(); ++i) {
		size_t j = (i + 1) % vertices.size();
		area += vertices[i].x * vertices[j].y - vertices[j].x * vertices[i].y;
	}

	// If the area is negative, CW
	if (area < 0.0f) {
		std::reverse(remaining_indices.begin(), remaining_indices.end());
	}

	size_t safety_counter = vertices.size() * 2;
	while (remaining_indices.size() > 3) {
		if (--safety_counter == 0) {
			break;
		}

		bool ear_found = false;
		size_t count = remaining_indices.size();

		for (size_t i = 0; i < count; ++i) {
			size_t prev_idx = remaining_indices[(i + count - 1) % count];
			size_t curr_idx = remaining_indices[i];
			size_t next_idx = remaining_indices[(i + 1) % count];

			const vec2& prev = vertices[prev_idx];
			const vec2& curr = vertices[curr_idx];
			const vec2& next = vertices[next_idx];

			vec2 edge1 = curr - prev;
			vec2 edge2 = next - curr;
			if (determinant(glm::mat2 { edge1, edge2 }) <= 0.00001f) {
				continue;
			}

			bool is_ear = true;
			for (size_t j = 0; j < count; ++j) {
				if (j == i || j == (i + count - 1) % count || j == (i + 1) % count) {
					continue;
				}

				size_t test_idx = remaining_indices[j];
				if (isPointInTriangle(vertices[test_idx], prev, curr, next)) {
					is_ear = false;
					break;
				}
			}

			if (is_ear) {
				indices.push_back(prev_idx);
				indices.push_back(curr_idx);
				indices.push_back(next_idx);

				remaining_indices.erase(remaining_indices.begin() + i);
				ear_found = true;
				break;
			}
		}

		if (!ear_found) {
			break;
		}
	}

	if (remaining_indices.size() == 3) {
		indices.push_back(remaining_indices[0]);
		indices.push_back(remaining_indices[1]);
		indices.push_back(remaining_indices[2]);
	}

	return indices;
}

void physics::ColliderRenderable::SendVertices(std::vector<glm::vec3>& points) {
	// clear things just in case
	m.points.clear();
	m.indices.clear();
	m.vertices.clear();
	m.topVertices.clear();
	m.topIndices.clear();

	this->m.points = points;
	this->m.indices = triangulate(points);
	// If triangulation failed fall back to a simple fan triangulation so we still have geometry to draw
	if (this->m.indices.empty() && this->m.points.size() >= 3) {
		TOAST_INFO("ColliderRenderable::SendVertices - triangulation failed, using fan fallback");
		for (uint16_t i = 1; i + 1 < this->m.points.size(); ++i) {
			this->m.indices.push_back(0);
			this->m.indices.push_back(i);
			this->m.indices.push_back(i + 1);
		}
	}
	CalculateBoundingBox();

	for (const auto& point : m.points) {
		m.vertices.emplace_back(
		    renderer::SpineVertex {
		      .position = glm::vec3 { point.x, point.y, 0.0 },
             .texCoord = { 0.0, 0.0 },
             .colorABGR = 0xFFFFFFFF
    }
		);
	}

	// TOAST_INFO("ColliderRenderable::SendVertices called (points={})", m.points.size());
	m.pending_update = true;

	if (m.showTop && m.points.size() >= 2) {
		float area = 0.0f;
		for (size_t i = 0; i < m.points.size(); ++i) {
			size_t j = (i + 1) % m.points.size();
			area += m.points[i].x * m.points[j].y - m.points[j].x * m.points[i].y;
		}
		float winding = (area >= 0.0f) ? 1.0f : -1.0f;

		float cos_threshold = cos(glm::radians(m.maxSlope));
		float current_distance = 0.0f;

		for (size_t i = 0; i < m.points.size(); ++i) {
			size_t next_idx = (i + 1) % m.points.size();
			glm::vec3 p1 = m.points[i] + m.horizontal_offset * glm::normalize(m.points[i] - m.points[next_idx]);
			glm::vec3 p2 = m.points[next_idx] + m.horizontal_offset * glm::normalize(m.points[next_idx] - m.points[i]);

			glm::vec3 edge = p2 - p1;
			float edge_len = glm::length(edge);
			if (edge_len < 0.0001f) {
				continue;
			}

			glm::vec2 normal2d = winding * glm::normalize(glm::vec2(p2.y - p1.y, p1.x - p2.x));
			glm::vec3 normal3d = glm::vec3(normal2d, 0.0f);

			// If normal points UP and within slope
			if (normal2d.y > cos_threshold) {
				uint16_t base_idx = static_cast<uint16_t>(m.topVertices.size());

				float next_distance = current_distance + edge_len;

				glm::vec3 offset_pos = normal3d * m.topOffset;

				// V0: Bottom Left
				m.topVertices.emplace_back(
				    renderer::SpineVertex {
				      .position = p1 + offset_pos, .texCoord = { current_distance, 0.0f },
                     .colorABGR = 0xFFFFFFFF
        }
				);
				// V1: Bottom Right
				m.topVertices.emplace_back(
				    renderer::SpineVertex {
				      .position = p2 + offset_pos, .texCoord = { next_distance, 0.0f },
                     .colorABGR = 0xFFFFFFFF
        }
				);
				// V2: Top Right
				m.topVertices.emplace_back(
				    renderer::SpineVertex {
				      .position = (p2 + offset_pos) + normal3d * m.topHeight, .texCoord = { next_distance, 1.0f },
                         .colorABGR = 0xFFFFFFFF
        }
				);
				// V3: Top Left
				m.topVertices.emplace_back(
				    renderer::SpineVertex {
				      .position = (p1 + offset_pos) + normal3d * m.topHeight, .texCoord = { current_distance, 1.0f },
                         .colorABGR = 0xFFFFFFFF
        }
				);

				m.topIndices.push_back(base_idx + 0);
				m.topIndices.push_back(base_idx + 1);
				m.topIndices.push_back(base_idx + 2);
				m.topIndices.push_back(base_idx + 0);
				m.topIndices.push_back(base_idx + 2);
				m.topIndices.push_back(base_idx + 3);

				current_distance = next_distance;
			}
		}

		m.pending_top_update = !m.topVertices.empty();
		if (m.pending_top_update && renderer::IRendererBase::GetInstance() != nullptr) {
			m.topMesh.InitDynamicSpine();
			if (!m.topVertices.empty() && !m.topIndices.empty()) {
				m.topMesh.UpdateDynamicSpine(m.topVertices.data(), m.topVertices.size(), m.topIndices.data(), m.topIndices.size());
				m.pending_top_update = false;
			}
		}
	}
}

void physics::ColliderRenderable::Init() {
	toast::TransformComponent::Init();
	// init just for loading
	m.material = resource::LoadResource<renderer::Material>(m.material_path);
	m.topMaterial = resource::LoadResource<renderer::Material>(m.topMaterialPath);
	m.occlusionShader = resource::LoadResource<renderer::Shader>("SHADERS/occlusion.shader");

	toast::Object::SetRunTick(false);
	toast::Object::SetRunEarlyTick(false);
	toast::Object::SetRunLateTick(false);

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

void physics::ColliderRenderable::Load(json_t j, bool force_create) {
	if (j.contains("type")) {
		toast::TransformComponent::Load(j, force_create);
	}
	m.material_path = j.value("material_path", "");
	m.showTop = j.value("showTop", false);
	m.maxSlope = j.value("maxSlope", 45.0f);
	m.topHeight = j.value("topHeight", 0.5f);
	m.topOffset = j.value("topOffset", -0.2f);
	m.topMaterialPath = j.value("topMaterialPath", "");
	m.isOccluder = j.value("isOccluder", false);
	m.show = j.value("show", true);
	m.horizontal_offset = j.value("horizontal_offset", .10f);
}

json_t physics::ColliderRenderable::Save() const {
	json_t j = toast::TransformComponent::Save();
	j["material_path"] = m.material_path;
	j["showTop"] = m.showTop;
	j["maxSlope"] = m.maxSlope;
	j["topHeight"] = m.topHeight;
	j["topOffset"] = m.topOffset;
	j["topMaterialPath"] = m.topMaterialPath;
	j["isOccluder"] = m.isOccluder;
	j["show"] = m.show;
	j["horizontal_offset"] = m.horizontal_offset;
	return j;
}

#ifdef TOAST_EDITOR
void physics::ColliderRenderable::Inspector() {
	m.material_slot.Show();
	ImGui::Separator();
	ImGui::Checkbox("Visible", &m.show);
	ImGui::Checkbox("Is Occluder", &m.isOccluder);
	ImGui::Checkbox("Show Top Layer", &m.showTop);
	if (m.showTop) {
		ImGui::DragFloat("Max Slope (Deg)", &m.maxSlope, 0.5f, 0.0f, 90.0f);
		ImGui::DragFloat("Top Height", &m.topHeight, 0.05f, 0.0f, 10.0f);
		ImGui::DragFloat("Top Offset", &m.topOffset, 0.05f, -10.0f, 10.0f);
		ImGui::DragFloat("Horizontal Offset", &m.horizontal_offset);
		m.topMaterialSlot.Show();
	}
}
#endif

void physics::ColliderRenderable::LoadTextures() {
	m.mesh.InitDynamicSpine();
	m.mesh.UpdateDynamicSpine(m.vertices.data(), m.vertices.size(), m.indices.data(), m.indices.size());

	m.topMesh.InitDynamicSpine();
	m.topMesh.UpdateDynamicSpine(m.topVertices.data(), m.topVertices.size(), m.topIndices.data(), m.topIndices.size());
}

void physics::ColliderRenderable::OnRender(renderer::IRenderablePass pass, const glm::mat4& view_projection) noexcept {
	if (not toast::Object::enabled() || not m.show) {
		return;
	}

	// Do this before frustum culling
	if (m.pending_update) {
		TOAST_INFO("ColliderRenderable::OnRender uploading mesh (verts={}, inds={})", m.vertices.size(), m.indices.size());
		m.mesh.InitDynamicSpine();
		m.mesh.UpdateDynamicSpine(m.vertices.data(), m.vertices.size(), m.indices.data(), m.indices.size());
		m.pending_update = false;
	}
	if (m.pending_top_update) {
		TOAST_INFO("ColliderRenderable::OnRender uploading top mesh (verts={}, inds={})", m.topVertices.size(), m.topIndices.size());
		m.topMesh.InitDynamicSpine();
		m.topMesh.UpdateDynamicSpine(m.topVertices.data(), m.topVertices.size(), m.topIndices.data(), m.topIndices.size());
		m.pending_top_update = false;
	}

	if (not OclussionVolume::isTransformedAABBOnPlanes(m.boundingBox, toast::TransformComponent::GetWorldMatrix())) {
		return;
	}

	// compute transform once
	const glm::mat4 model = toast::TransformComponent::GetWorldMatrix();
	const glm::mat4 mvp = view_projection * model;

	if (m.material && m.material->GetShader()) {
		m.material->Use();
		if (pass == renderer::IRenderablePass::GEOMETRY) {
			auto shader = m.material->GetShader();
			shader->Set("gWorld", model);
			shader->Set("gMVP", mvp);
		} else if (pass == renderer::IRenderablePass::OCCLUSION) {
			if (!m.isOccluder) {
				return;
			}

			m.occlusionShader->Use();
			m.occlusionShader->Set("gWorld", model);
			m.occlusionShader->Set("gMVP", mvp);
		}
		// draw
		m.mesh.DrawDynamicSpine(m.indices.size());
	}

	if (m.showTop && m.topMaterial && m.topMaterial->GetShader()) {
		m.topMaterial->Use();
		if (pass == renderer::IRenderablePass::GEOMETRY) {
			auto shader = m.topMaterial->GetShader();
			shader->Set("gWorld", model);
			shader->Set("gMVP", mvp);
		} else if (pass == renderer::IRenderablePass::OCCLUSION) {
			m.occlusionShader->Use();
			m.occlusionShader->Set("gWorld", model);
			m.occlusionShader->Set("gMVP", mvp);
		}
		m.topMesh.DrawDynamicSpine(m.topIndices.size());
	}
}

void physics::ColliderRenderable::CalculateBoundingBox() {
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
		.min = { x_min, y_min, 0.0f },
      .max = { x_max, y_max, 0.0f }
	};
}
