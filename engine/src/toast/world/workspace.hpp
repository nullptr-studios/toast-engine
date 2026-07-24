/**
 * @file Workspace.hpp
 * @author Xein
 * @date 15 Jun 2026
 *
 * @brief Reduced @c World class for the editor viewport
 *
 * Doesn't multithread anything or handle ticking
 */

#pragma once

#include "node_owner.hpp"

#include <memory>
#include <toast/events/listener.hpp>

namespace toast {
/**
 * @brief Single-viewport node owner used by the editor
 *
 * A lightweight alternative to World: owns a node tree through the INodeOwner interface
 * but never ticks nodes, never builds a dependency graph, and never runs lifecycle functions
 * beyond those triggered by instantiation. Use it to display or edit a node tree without
 * running any game logic.
 *
 * @see World, INodeOwner
 */
class Workspace : public INodeOwner {
public:
	/**
	 * @brief Creates a new workspace rooted at a single fresh node of the given type
	 * @param type Fully-qualified C++ class name for the root node, e.g. "toast::Node3D"
	 * @param handle UID the editor assigned to this workspace; stored for round-trip serialization
	 */
	Workspace(std::string_view type, UID handle);

	/**
	 * @brief Opens an existing workspace by loading the prefab at the given UID
	 * @param uid Asset UID of a previously saved workspace prefab
	 */
	Workspace(UID uid);

	/**
	 * @brief Opens a workspace bound to the given asset UID but loading its content from another file
	 * @param uid Asset UID of the workspace prefab; used as the workspace handle
	 * @param source_uri Virtual path to read the prefab bytes from
	 *
	 * Used to recover autosaves
	 */
	Workspace(UID uid, std::string_view source_uri);

	~Workspace() override;

	auto name() -> std::string override;

	/// No-op; Workspace has no tick scheduler and never registers dependencies
	void registerDependency(Node& from, Node& to) override;
	void unregisterDependency(Node& from, Node& to) override;

	[[nodiscard]]
	auto participatesIn(NodeOwnerParticipation use) const noexcept -> bool override;

	/// Name lookup over origin's subtree
	auto findFrom(const Node& origin, std::string_view query) -> Box<Node> override;

	/// UID lookup over origin's subtree
	auto findFrom(const Node& origin, const UID& uid) -> Box<Node> override;

	/// Same query grammar as World::searchFrom(); searches only within m_root_node
	auto searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> override;

protected:
	/// disambiguates the protected ctor from Workspace(UID)
	struct EmptyTag { };

	/// Sets only the handle and subscribes to editor events; used by PlayWorkspace
	Workspace(UID handle, EmptyTag);

	UID m_handle;
	Box<Node> m_root_node;
	Box<Node> m_focused_node;
	event::Listener m_listener;

	// mirrored editor toolbar state; defaults match the editor's
	struct SnapSetting {
		bool enabled = true;
		float value = 0.0f;
	};

	enum class GizmoTool : uint8_t {
		select,
		translate,
		rotate,
		scale,
		ruler
	};

	GizmoTool m_gizmo_tool = GizmoTool::select;
	bool m_world_space = false;    ///< false = local coordinates
	SnapSetting m_translate_snap {true, 0.10f};
	SnapSetting m_rotate_snap {true, 30.0f};
	SnapSetting m_scale_snap {true, 0.10f};
	bool m_game_camera = false;    ///< false = editor camera
	std::unique_ptr<Camera> m_editor_camera;

	[[nodiscard]]
	auto isActiveWorkspace() const noexcept -> bool;

	void eventSubscriptions();

	/// instantiates the prefab and sets up the root node
	void initFromPrefab(const assets::Handle<assets::Prefab>& file);
	void applyActiveCamera() override;

private:
	double m_inspector_accum = 0.0;

public:
	/**
	 * @brief Streams the focused node's reflected values to the editor at a fixed rate
	 *
	 * Workspace nodes are never ticked for game logic; this override only throttles and emits
	 * InspectorContent for the active workspace's focused node. It is a no-op when this workspace
	 * is not the active one or when no node is focused.
	 */
	void tick() override;

	[[nodiscard]]
	auto rootNode() const -> const Node& {
		return *m_root_node;
	}

	[[nodiscard]]
	auto isValid() const -> bool {
		return m_root_node.exists();
	}
};
}
