/// @file Mesh.cpp
/// @author dario
/// @date 27/09/2025.

#define GLM_ENABLE_EXPERIMENTAL

#include "Engine/Core/Log.hpp"
#include "Engine/Resources/ResourceManager.hpp"
#include "glm/gtx/norm.hpp"

#include <Engine/Resources/Mesh.hpp>
#include <glm/glm.hpp>
#include <tiny_obj_loader.h>

renderer::Mesh::Mesh(Mesh&& o) noexcept
    : m_vertices(o.m_vertices),
      m_vao(o.m_vao),
      m_vbo(o.m_vbo),
      m_ebo(o.m_ebo),
      m_debugName(std::move(o.m_debugName)),
      m_hasVertexColor(o.m_hasVertexColor) {
	m_path = std::move(o.m_path);

	o.m_vao = 0;
	o.m_vbo = 0;
	o.m_ebo = 0;
	o.m_hasVertexColor = false;
}

renderer::Mesh& renderer::Mesh::operator=(Mesh&& o) noexcept {
	if (this != &o) {
		m_vao = o.m_vao;
		m_vbo = o.m_vbo;
		m_ebo = o.m_ebo;
		m_vertices = o.m_vertices;
		m_path = std::move(o.m_path);
		m_debugName = std::move(o.m_debugName);
		m_hasVertexColor = o.m_hasVertexColor;

		// invalidate other shader
		o.m_vao = 0;
		o.m_vbo = 0;
		o.m_ebo = 0;
		o.m_hasVertexColor = false;
	}
	return *this;
}

renderer::Mesh::~Mesh() {
	if (m_vao) {
		glDeleteVertexArrays(1, &m_vao);
	}
	if (m_vbo) {
		glDeleteBuffers(1, &m_vbo);
	}
	if (m_ebo) {
		glDeleteBuffers(1, &m_ebo);
	}
}

void renderer::Mesh::Load() {
	PROFILE_ZONE;
	SetResourceState(resource::ResourceState::LOADING);

	std::istringstream stream {};
	if (!resource::ResourceManager::GetInstance()->OpenFile(m_path, stream)) {
		throw ToastException("Mesh: Failed to open mesh file: " + m_path);
	}

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;
	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, &stream);

	if (!warn.empty()) {
		TOAST_WARN("TinyObjLoader warning: {0}", warn);
	}
	if (!err.empty()) {
		TOAST_ERROR("TinyObjLoader error: {0}", err);
	}
	if (!ret) {
		throw ToastException("TinyObjLoader failed to load mesh: " + m_path);
	}

	size_t total_indices = 0;
	for (const auto& shape : shapes) {
		total_indices += shape.mesh.indices.size();
	}
	m_vertices.reserve(total_indices);

	for (const auto& shape : shapes) {
		const auto& mesh = shape.mesh;
		for (const auto& idx : mesh.indices) {
			glm::vec3 position(0.0f), normal(0.0f);
			glm::vec2 tex_coord(0.0f);

			if (idx.vertex_index >= 0) {
				position = { attrib.vertices[(3 * idx.vertex_index) + 0],
					           attrib.vertices[(3 * idx.vertex_index) + 1],
					           attrib.vertices[(3 * idx.vertex_index) + 2] };
			}
			if (idx.normal_index >= 0) {
				normal = { attrib.normals[(3 * idx.normal_index) + 0],
					         attrib.normals[(3 * idx.normal_index) + 1],
					         attrib.normals[(3 * idx.normal_index) + 2] };
			}
			if (idx.texcoord_index >= 0) {
				tex_coord = { attrib.texcoords[(2 * idx.texcoord_index) + 0], attrib.texcoords[(2 * idx.texcoord_index) + 1] };
			}
			m_vertices.emplace_back(Vertex { position, normal, tex_coord, {} });
		}
	}
	ComputeTangents(m_vertices);
	SetResourceState(resource::ResourceState::LOADEDCPU);
}

void renderer::Mesh::LoadMainThread() {
	PROFILE_ZONE;
	SetResourceState(resource::ResourceState::UPLOADING);
	if (m_vertices.empty()) {
		throw ToastException("Mesh: Failed to load mesh");
	}

	glGenVertexArrays(1, &m_vao);
	glBindVertexArray(m_vao);

	glGenBuffers(1, &m_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

	glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * m_vertices.size(), m_vertices.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texCoord)));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, tangent)));

	glBindVertexArray(0);

	SetResourceState(resource::ResourceState::UPLOADEDGPU);
}

void renderer::Mesh::bind() const {
	if (!m_vao) {
		throw ToastException("Mesh: Failed to bind mesh");
	}
	glBindVertexArray(m_vao);
}

void renderer::Mesh::unbind() {
	glBindVertexArray(0);
}

void renderer::Mesh::Draw() {
	bind();
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));
	unbind();
}

void renderer::Mesh::InitDynamicSpine() {
	if (!m_vao) {
		glGenVertexArrays(1, &m_vao);
	}
	if (!m_vbo) {
		glGenBuffers(1, &m_vbo);
	}
	if (!m_ebo) {
		glGenBuffers(1, &m_ebo);
	}

	glBindVertexArray(m_vao);

	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SpineVertex), reinterpret_cast<void*>(offsetof(SpineVertex, position)));

	// Keep texcoord at location 2 to avoid colliding with regular mesh layout (location 1 is normal on static meshes)
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SpineVertex), reinterpret_cast<void*>(offsetof(SpineVertex, texCoord)));

	// Enable color attribute for spine (packed ABGR in uint32_t). Use unsigned byte normalized to vec4 in shader.
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(SpineVertex), reinterpret_cast<void*>(offsetof(SpineVertex, colorABGR)));

	glBindVertexArray(0);
}

void renderer::Mesh::UpdateDynamicSpine(const SpineVertex* vertices, size_t num_vertices, const uint16_t* indices, size_t num_indices) const {
	glBindVertexArray(m_vao);

	// Orphan the buffers
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	GLsizeiptr verts_size = static_cast<GLsizeiptr>(sizeof(SpineVertex) * num_vertices);
	glBufferData(GL_ARRAY_BUFFER, verts_size, nullptr, GL_DYNAMIC_DRAW);
	if (verts_size > 0) {
		glBufferSubData(GL_ARRAY_BUFFER, 0, verts_size, vertices);
	}

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
	GLsizeiptr idx_size = static_cast<GLsizeiptr>(sizeof(uint16_t) * num_indices);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_size, nullptr, GL_DYNAMIC_DRAW);
	if (idx_size > 0) {
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_size, indices);
	}

	glBindVertexArray(0);
}

void renderer::Mesh::DrawDynamicSpine(size_t num_indices) const {
	if (m_vao == 0) {
		TOAST_ERROR("Mesh::DrawDynamicSpine called but VAO==0. Did you call InitDynamicSpine?");
		return;
	}
	glBindVertexArray(m_vao);
	glDisable(GL_CULL_FACE);
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(num_indices), GL_UNSIGNED_SHORT, nullptr);
	glEnable(GL_CULL_FACE);
	glBindVertexArray(0);
}

void renderer::Mesh::ComputeTangents(std::vector<Vertex>& verts) {
	size_t vcount = verts.size();
	if (vcount < 3) {
		return;
	}

	std::vector<glm::vec3> tan_accum(vcount, glm::vec3(0.0f));
	std::vector<glm::vec3> bitan_accum(vcount, glm::vec3(0.0f));

	for (size_t i = 0; i + 2 < vcount; i += 3) {
		const glm::vec3& p0 = verts[i + 0].position;
		const glm::vec3& p1 = verts[i + 1].position;
		const glm::vec3& p2 = verts[i + 2].position;

		const glm::vec2& uv0 = verts[i + 0].texCoord;
		const glm::vec2& uv1 = verts[i + 1].texCoord;
		const glm::vec2& uv2 = verts[i + 2].texCoord;

		glm::vec3 dp1 = p1 - p0;
		glm::vec3 dp2 = p2 - p0;
		glm::vec2 duv1 = uv1 - uv0;
		glm::vec2 duv2 = uv2 - uv0;

		float denom = (duv1.x * duv2.y) - (duv2.x * duv1.y);
		float r = (fabs(denom) > 1e-8f) ? (1.0f / denom) : 0.0f;

		glm::vec3 tangent = (dp1 * duv2.y - dp2 * duv1.y) * r;
		glm::vec3 bitangent = (dp2 * duv1.x - dp1 * duv2.x) * r;

		tan_accum[i + 0] += tangent;
		tan_accum[i + 1] += tangent;
		tan_accum[i + 2] += tangent;

		bitan_accum[i + 0] += bitangent;
		bitan_accum[i + 1] += bitangent;
		bitan_accum[i + 2] += bitangent;
	}

	for (size_t i = 0; i < vcount; ++i) {
		glm::vec3 n = verts[i].normal;
		glm::vec3 t = tan_accum[i];

		// If t is nearly zero, provide a fallback
		if (glm::length2(t) < 1e-12f) {
			glm::vec3 up = fabs(n.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
			t = glm::normalize(glm::cross(up, n));
		} else {
			t = glm::normalize(t - n * glm::dot(n, t));
		}

		glm::vec3 b = glm::cross(n, t);
		float handedness = (glm::dot(b, bitan_accum[i]) < 0.0f) ? -1.0f : 1.0f;

		verts[i].tangent = glm::vec4(t, handedness);
	}
}
