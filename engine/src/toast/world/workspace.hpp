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

#include "editor_camera.hpp"
#include "gizmo_layout.hpp"
#include "node_owner.hpp"
#include "workspace_history.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <toast/events/listener.hpp>
#include <utility>

namespace toast {
/**
 * @brief Single-viewport node owner used by the editor
 *
 * A lightweight alternative to World: owns a node tree through the INodeOwner interface
 * but never ticks nodes, never builds a dependency graph, and never runs lifecycle functions
 * beyond those triggered by instantiation. Use it to display or edit a node tree without
 * running any game logic
 *
 * @see World, INodeOwner
 */
class TOAST_API Workspace : public INodeOwner {
public:
	struct SizeHandlePoint {
		glm::vec3 world_position {0.0f};
		GizmoHandle handle = GizmoHandle::none;
	};

	static constexpr size_t k_max_size_handles = 6;

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

	auto asWorkspace() -> Workspace* override { return this; }

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

	/// @brief True for PlayWorkspace, gates gizmo interaction off so it can never run against a live game
	[[nodiscard]]
	virtual auto isPlaying() const -> bool {
		return false;
	}

	void inheritEditorCamera(const Workspace& source);
	void preparePrefabReload(UID uid);
	void finishPrefabReload();

protected:
	[[nodiscard]]
	auto owningPrefabUid() const -> UID override {
		return m_handle;
	}

	/// disambiguates the protected ctor from Workspace(UID)
	struct EmptyTag { };

	/// Sets only the handle and subscribes to editor events; used by PlayWorkspace
	Workspace(UID handle, EmptyTag);

	UID m_handle;
	// In-memory source assets must outlive the handles held by the node tree.
	std::unique_ptr<assets::Prefab> m_owned_source_prefab;
	Box<Node> m_root_node;
	Box<Node> m_focused_node;
	event::Listener m_listener;

	// mirrored editor toolbar state; defaults match the editor's
	struct SnapSetting {
		bool enabled = true;
		float value = 0.0f;
	};

	GizmoTool m_gizmo_tool = GizmoTool::select;
	bool m_world_space = false;    ///< false = local coordinates
	SnapSetting m_translate_snap {true, 0.10f};
	SnapSetting m_rotate_snap {true, 30.0f};
	SnapSetting m_scale_snap {true, 0.10f};
	bool m_game_camera = false;                           ///< false = editor camera
	std::unique_ptr<Camera> m_editor_camera;
	EditorCameraController m_editor_camera_controller;    ///< drives m_editor_camera; ticked from tick() below
	std::unique_ptr<WorkspaceHistory> m_history;

	[[nodiscard]]
	auto isActiveWorkspace() const noexcept -> bool;

	/// @brief The gizmo's current state
	// Translate-gizmo interaction
	glm::vec2 m_gizmo_mouse_pos {0.0f, 0.0f};
	GizmoHandle m_gizmo_hover = GizmoHandle::none;
	GizmoHandle m_gizmo_drag = GizmoHandle::none;
	glm::vec3 m_gizmo_drag_start_world_pos {0.0f};
	glm::quat m_gizmo_drag_start_rotation {1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec3 m_gizmo_drag_start_scale {1.0f};
	glm::vec3 m_gizmo_drag_anchor {0.0f};          ///< closest-point-on-axis or ray-plane hit at drag start
	glm::vec3 m_gizmo_drag_axis {0.0f};            ///< world-space axis direction, for axis/ring drags
	glm::vec3 m_gizmo_drag_plane_normal {0.0f};    ///< world-space plane normal, for plane/center/ring drags
	float m_gizmo_drag_start_angle = 0.0f;         ///< radians, for rotate drags
	float m_gizmo_drag_current_factor = 1.0f;      ///< live scale factor, for the scale feedback stretch

	glm::vec3 m_gizmo_drag_start_size {0.0f};
	glm::vec3 m_gizmo_drag_start_local_pos {0.0f};
	glm::quat m_gizmo_drag_start_local_rot {1.0f, 0.0f, 0.0f, 0.0f};
	/// dragged field's value at drag start, set while the drag owns the open history transaction
	std::optional<std::string> m_gizmo_history_start;

	[[nodiscard]]
	auto gizmoOrigin() const -> glm::vec3;
	[[nodiscard]]
	auto gizmoOrientation() const -> glm::quat;
	[[nodiscard]]
	auto gizmoScale() const -> float;
	[[nodiscard]]
	auto gizmoInteractionAllowed() const -> bool;

	[[nodiscard]]
	auto collectSizeHandles() const -> std::pair<std::array<SizeHandlePoint, k_max_size_handles>, uint32_t>;

	void gizmoApplySizeDrag(float delta);
	void gizmoUpdateHover();
	void gizmoBeginDrag(GizmoHandle handle);
	void gizmoUpdateDrag();
	void gizmoEndDrag();

	void eventSubscriptions();
	void initializeHistory(bool available, bool initially_saved);
	auto restoreHistorySnapshot(const assets::Prefab& snapshot) -> bool;
	void destroyOwnedTree(Box<Node>& root);

	template<typename F>
	void recordHistory(WorkspaceHistory::Context context, F&& mutation) {
		const bool owned = m_history && m_history->beginAtomic(std::move(context));
		std::forward<F>(mutation)();
		if (m_history) {
			m_history->finishAtomic(owned);
		}
	}

	/// instantiates the prefab and sets up the root node
	void initFromPrefab(const assets::Handle<assets::Prefab>& file);
	void applyActiveCamera() override;

private:
	struct PendingPrefabReload {
		Box<Node> node;
		assets::Prefab reference;
	};

	std::vector<PendingPrefabReload> m_prefab_reloads;
	double m_inspector_accum = 0.0;

	/**
	 * @brief Recursively advances every AnimationPlayer under @p node by one frame
	 *
	 * A Workspace never runs the tick scheduler, so without this nothing animates outside play mode.
	 * PlayWorkspace skips it, or a clip would be sampled twice in one frame
	 */
	void tickAnimationPreviews(const Node& node);

public:
	/**
	 * @brief Editor-only per-frame work: camera preview, animation preview, inspector streaming
	 *
	 * Not the gameplay tick scheduler - only what the viewport needs to stay live. See PlayWorkspace::tick()
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

	struct GizmoRenderState {
		bool visible = false;
		GizmoTool tool = GizmoTool::select;
		glm::vec3 origin {0.0f};
		glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};
		GizmoHandle hover = GizmoHandle::none;
		GizmoHandle active = GizmoHandle::none;
		float drag_scale_factor = 1.0f;    ///< scale tool only: live multiplicative factor for the active handle

		std::array<SizeHandlePoint, k_max_size_handles> size_handles {};
		uint32_t size_handle_count = 0;
		float size_handle_scale = 1.0f;
	};

	/// @brief snapshot of the active gizmo's transform/highlight state, resolved on the main thread for Engine::tick() to hand to
	/// VulkanRenderer
	/// @note See docs/renderer.md RenderFrame pattern
	[[nodiscard]]
	auto gizmoRenderState() const -> GizmoRenderState;
};
}
