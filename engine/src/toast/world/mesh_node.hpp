/**
 * @file mesh_node.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "node_3d.hpp"

#include <toast/assets/types.hpp>

namespace assets {
class Mesh;
class Material;
}

namespace toast {
class [[ToastNode, Icon("MeshItem")]] TOAST_API MeshNode : public Node3D {
public:
	MeshNode() = default;

	MeshNode(assets::Handle<assets::Mesh> mesh) : m_mesh(std::move(mesh)) { }

	MeshNode(assets::Handle<assets::Mesh> mesh, assets::Handle<assets::Material> material)
	    : m_mesh(std::move(mesh)),
	      m_material(std::move(material)) { }

	auto worldTransformForRender() -> const glm::mat4& { return getWorldTransform(); }

	[[nodiscard]]
	auto getMesh() const -> const assets::Handle<assets::Mesh>& {
		return m_mesh;
	}

	[[nodiscard]]
	auto getMesh() -> assets::Handle<assets::Mesh>& {
		return m_mesh;
	}

	[[nodiscard]]
	auto getMaterial() const -> const assets::Handle<assets::Material>& {
		return m_material;
	}

	[[nodiscard]]
	auto getMaterial() -> assets::Handle<assets::Material>& {
		return m_material;
	}

private:
	void init();
	void end();
	void destroy();

	[[Reflect]]
	assets::Handle<assets::Mesh> m_mesh;

	[[Reflect]]
	assets::Handle<assets::Material> m_material;

	bool m_registered_proxy = false;
};
}
