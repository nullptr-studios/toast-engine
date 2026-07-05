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
}

namespace toast {
class [[ToastNode, Icon("MeshItem")]] MeshNode : public Node3D {
public:
	MeshNode() = default;

	MeshNode(assets::AssetHandle<assets::Mesh> mesh) : m_mesh(std::move(mesh)) { }

	auto worldTransformForRender() -> const glm::mat4& { return getWorldTransform(); }

	[[nodiscard]]
	auto getMesh() const -> const assets::AssetHandle<assets::Mesh>& {
		return m_mesh;
	}

	[[nodiscard]]
	auto getMesh() -> assets::AssetHandle<assets::Mesh>& {
		return m_mesh;
	}

private:
	void init();
	void end();
	void destroy();

	[[Reflect]]
	assets::AssetHandle<assets::Mesh> m_mesh;

	bool m_registered_proxy = false;
};
}
