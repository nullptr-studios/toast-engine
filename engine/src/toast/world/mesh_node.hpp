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
#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Icon("MeshItem")]] TOAST_API MeshNode : public Node3D {
public:
	MeshNode() = default;

private:
	[[Reflect]]
	assets::AssetHandle<assets::Mesh> m_mesh;
};
}
