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

/// @struct BoundingBox
/// @brief Axis-aligned bounding box for mesh geometry
struct BoundingBox {
	glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
	glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());

	/// Returns the center of the bounding box
	[[nodiscard]]
	glm::vec3 center() const {
		return (min + max) * 0.5f;
	}

	/// Returns the size (extents) of the bounding box
	[[nodiscard]]
	glm::vec3 size() const {
		return max - min;
	}

	/// Returns the half-extents of the bounding box
	[[nodiscard]]
	glm::vec3 halfExtents() const {
		return size() * 0.5f;
	}

	/// Returns the radius of the bounding sphere that encompasses the box
	[[nodiscard]]
	float boundingSphereRadius() const {
		return glm::length(halfExtents());
	}

	/// Expands the bounding box to include a point
	void expand(const glm::vec3& point) {
		min = glm::min(min, point);
		max = glm::max(max, point);
	}

	/// Returns true if the bounding box is valid (min <= max)
	[[nodiscard]]
	bool isValid() const {
		return min.x <= max.x && min.y <= max.y && min.z <= max.z;
	}
};

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

	/// Computes bounding box from an array of SpineVertex (for dynamic meshes)
	/// Returns the computed bounding box and also caches it internally
	BoundingBox ComputeSpineBoundingBox(const SpineVertex* vertices, size_t num_vertices);

	/// Returns the last computed dynamic bounding box (from ComputeSpineBoundingBox)
	[[nodiscard]]
	const BoundingBox& dynamicBoundingBox() const {
		return m_dynamicBoundingBox;
	}

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

	// Return mesh bounding box in object/model space (computed at load time)
	[[nodiscard]]
	const BoundingBox& boundingBox() const {
		return m_boundingBox;
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

	// Mesh bounding box in object space (computed during Load)
	BoundingBox m_boundingBox;

	// Dynamic bounding box for spine/animated meshes (computed per-frame via ComputeSpineBoundingBox)
	BoundingBox m_dynamicBoundingBox;
};

}    // namespace renderer
