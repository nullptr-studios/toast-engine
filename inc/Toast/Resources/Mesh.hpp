//
// Created by dario on 17/09/2025.
//

#pragma once

#include "IResource.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace renderer {

///@enum Vertex
///@brief Vertex structure used for meshes
///@TODO: add color support
struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 texCoord;
	glm::vec4 tangent;    // xyz = tangent, w = handedness
};

struct SpineVertex {
	glm::vec3 position;
	glm::vec2 texCoord;
	uint32_t colorABGR;
};

///@class Mesh
///@brief Mesh resource class
class Mesh : public IResource {
public:
	Mesh(std::string path = "") : IResource(std::move(path), resource::ResourceType::MODEL, true) { }

	// Moveable
	Mesh(Mesh&& o) noexcept;
	Mesh& operator=(Mesh&& o) noexcept;

	// Non-copyable
	Mesh(const Mesh&) = delete;
	Mesh& operator=(const Mesh&) = delete;

	~Mesh() override;

	void Load() override;
	void LoadMainThread() override;

	/// Bind/unbind VAO
	void bind() const;
	void unbind();

	///@brief Draws the mesh
	void Draw();

	/// Debug name
	void setDebugName(std::string name) {
		m_debugName = std::move(name);
	}

	[[nodiscard]]
	const std::string& debugName() const noexcept {
		return m_debugName;
	}

	void InitDynamicSpine();
	void UpdateDynamicSpine(const SpineVertex* vertices, size_t num_vertices, const uint16_t* indices, size_t num_indices) const;
	void DrawDynamicSpine(size_t num_indices) const;

	void setHasVertexColor(bool v) {
		m_hasVertexColor = v;
	}

	[[nodiscard]]
	bool hasVertexColor() const {
		return m_hasVertexColor;
	}

	// Return mesh centroid in object/model space (computed at load time)
	[[nodiscard]]
	const glm::vec3& centroid() const {
		return m_centroid;
	}

	// Return number of vertices
	[[nodiscard]]
	size_t GetVertexCount() const {
		return m_vertices.size();
	}

private:
	void LoadErrMeshPlaceholder();

	void ComputeTangents(std::vector<Vertex>& verts);

	std::vector<Vertex> m_vertices;

	// GPU handles
	GLuint m_vao = 0;
	GLuint m_vbo = 0;
	GLuint m_ebo = 0;

	std::string m_debugName;

	// Whether this mesh provides per-vertex colors in Vertex::colorARGB
	bool m_hasVertexColor = false;

	// Mesh centroid in object space (computed during Load)
	glm::vec3 m_centroid = glm::vec3(0.0f);
};

}    // namespace renderer
