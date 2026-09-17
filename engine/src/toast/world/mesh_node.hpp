/**
 * @file mesh_node.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief Scene-graph node that draws an assets::Mesh
 */

#pragma once
#include "node_3d.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <toast/assets/animation.hpp>
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

	/**
	 * @brief Which .tanim asset's Skin drives this mesh, if it's skinned
	 *
	 * Only meaningful when getMesh()->isSkinned(). The skeleton that actually poses the joints isn't
	 * referenced here - the renderer looks for the nearest toast::AnimationPlayer ancestor and resolves
	 * this Skin's joint names against its subtree, matching how AnimationPlayer itself resolves clip
	 * targets. That keeps this a data-only reference instead of a second cross-node link to keep in sync
	 */
	[[nodiscard]]
	auto getSkinAnimation() const -> const assets::Handle<assets::Animation>& {
		return m_skin_animation;
	}

	/// @returns which Skin inside skinAnimation() to use; empty means "the first one"
	[[nodiscard]]
	auto getSkinName() const -> const std::string& {
		return m_skin_name;
	}

private:
	void updateInspectorMessages() override;
	void init();
	void end();
	void destroy();

	[[Reflect]]
	assets::Handle<assets::Mesh> m_mesh;

	[[Reflect]]
	assets::Handle<assets::Material> m_material;

	[[Reflect, Name("Skin Animation")]]
	assets::Handle<assets::Animation> m_skin_animation;

	[[Reflect, Name("Skin Name")]]
	std::string m_skin_name;

	bool m_registered_proxy = false;
};
}

#include <meshnode.generated.hpp>
