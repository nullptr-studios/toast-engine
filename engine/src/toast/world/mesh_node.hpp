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
class [[ToastNode, Icon("MeshItem")]] MeshNode : public Node3D {
public:
	MeshNode() = default;

	MeshNode(assets::AssetHandle<assets::Mesh> mesh) : m_mesh(std::move(mesh)) { }

	MeshNode(assets::AssetHandle<assets::Mesh> mesh, assets::AssetHandle<assets::Material> material)
	    : m_mesh(std::move(mesh)),
	      m_material(std::move(material)) { }

	auto worldTransformForRender() -> const glm::mat4& { return getWorldTransform(); }

	[[nodiscard]]
	auto getMesh() const -> const assets::AssetHandle<assets::Mesh>& {
		return m_mesh;
	}

	[[nodiscard]]
	auto getMesh() -> assets::AssetHandle<assets::Mesh>& {
		return m_mesh;
	}

	[[nodiscard]]
	auto getMaterial() const -> const assets::AssetHandle<assets::Material>& {
		return m_material;
	}

	[[nodiscard]]
	auto getMaterial() -> assets::AssetHandle<assets::Material>& {
		return m_material;
	}

private:
	void init();
	void end();
	void destroy();

	[[Reflect]]
	assets::AssetHandle<assets::Mesh> m_mesh;

	[[Reflect]]
	assets::AssetHandle<assets::Material> m_material;

	bool m_registered_proxy = false;
};
}
