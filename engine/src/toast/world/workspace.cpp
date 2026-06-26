#include "workspace.hpp"

#include "node.hpp"
#include "toast/engine.hpp"
#include "toast/log.hpp"
#include "workspace_events.hpp"
#include "workspace_events.pb.h"

#include <format>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <limits>
#include <sstream>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/assets/prefab.hpp>
#include <toast/time.hpp>

namespace toast {

Workspace::Workspace(std::string_view type, UID handle) : m_handle(handle) {
	ZoneScoped;
	eventSubscriptions();

	// Allocation
	Box node = this->nodeAllocation(type);
	node->propagateCallTick(node->info(), TickFunctionList::load);
	node->propagateCallTick(node->info(), TickFunctionList::pre_init);

	// Data structure generation
	generateUid(node);
	node->m_parent = {};
	node->m_state = NodeState::root;
	node->m_type = NodeType::world_root;
	node->m_inherited_enabled = true;
	node->m_name = stripNamespace(node->info()->type);

	// Initialization
	node->callTick(node->info(), TickFunctionList::init);
	node->callTick(node->info(), TickFunctionList::begin);
	node->enabled(true);

	m_root_node = node;
	event::send<event::RequestHierarchyUpdate>();
	TOAST_INFO("World", "Created new workspace");
}

Workspace::Workspace(UID uid) : m_handle(uid) {
	eventSubscriptions();

	// open file
	auto file = assets::load<assets::Prefab>(uid);
	if (not file.hasValue()) {
		TOAST_ERROR("World", "Couldn't open Node file {}", uid);
		return;
	}

	INodeOwner::InstantiateContext ctx;
	ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
	Box<Node> node = instantiate(file, ctx);
	if (not node.exists()) {
		TOAST_ERROR("World", "Failed to instantiate node {}", uid);
		return;
	}
	node->propagateCallTick(node->info(), TickFunctionList::load);
	node->propagateCallTick(node->info(), TickFunctionList::pre_init);

	// Data structure generation
	generateUid(node);
	node->m_parent = {};
	node->m_state = NodeState::root;
	node->m_type = NodeType::world_root;
	node->m_inherited_enabled = true;

	// Initialization
	node->callTick(node->info(), TickFunctionList::init);
	node->callTick(node->info(), TickFunctionList::begin);
	node->enabled(true);

	m_root_node = node;
	event::send<event::RequestHierarchyUpdate>();
	TOAST_INFO("World", "Opened workspace from {}", uid);
}

auto Workspace::name() -> std::string {
	return std::string {m_root_node->name()};
}

void Workspace::registerDependency(Node& from, Node& to) {
	// TODO:
	// TOAST_NOT_IMPLEMENTED;
}

auto Workspace::findFrom(const Node& origin, std::string_view query) -> Box<Node> {
	uint64_t target = UID::fromString(query);

	auto search = [target](this auto&& self, const Node& node) -> Box<Node> {
		if (node.m_uid.data() == target) {
			return node.box();
		}
		for (const auto& c : node.m_children) {
			if (auto found = self(*c); found.exists()) {
				return found;
			}
		}
		return {};
	};

	return search(origin);
}

auto Workspace::searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> {
	// TODO:
	TOAST_NOT_IMPLEMENTED;
	return {};
}

void Workspace::eventSubscriptions() {
	// Whenever we get notified that the hierarchy needs an update, send the update back to the editor
	m_listener.subscribe<event::RequestHierarchyUpdate>(
	    [this] {
		    if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			    return false;
		    }

		    event::send<event::UpdateHierarchyData>(m_root_node);
		    return true;
	    },
	    100
	);

	m_listener.subscribe<event::WorkspaceCreateNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		TOAST_ASSERT(m_root_node.exists(), "World", "Trying to add a node to an invalid Workspace");

		auto parent = findFrom(m_root_node, e.parent.get());
		if (not parent.exists()) {
			TOAST_WARN("World", "Tried to create node on Workspace {} but parent couldn't be found", m_root_node->name());
			return true;
		}

		auto node = requestRuntimeCreate(parent, e.type);
		TOAST_INFO("World", "Created node {} in Workspace {}", node->name(), m_root_node->name());
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});

	m_listener.subscribe<event::WorkspaceRemoveNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		TOAST_ASSERT(m_root_node.exists(), "World", "Trying to add a node to an invalid Workspace");

		auto node = findFrom(m_root_node, e.target.get());
		if (not node.exists() || not node->parentInternal().exists()) {
			TOAST_WARN("World", "Tried to remove node on Workspace {} but the node couldn't be found", m_root_node->name());
			return true;
		}

		auto parent = node->parentInternal();
		auto name = std::string {node->name()};

		// Detach from the parent so the editor no longer reaches the subtree
		std::erase(parent->m_children, node);

		// Collect the whole subtree into raw pointers
		std::vector<Node*> victims;
		auto collect = [&victims](this auto&& self, Node& n) -> void {
			victims.push_back(&n);
			for (auto& c : n.m_children) {
				self(*c);
			}
		};
		collect(*node);
		node = {};    // drop our own reference; nothing external holds the subtree now

		// Free every node in place
		for (Node* victim : victims) {
			_detail::ControlBox* control = _detail::ControlBox::get(victim);
			const NodeInfo* info = victim->info();

			victim->m_parent = {};
			victim->m_children.clear();
			victim->m_listener.reset();

			if (info && info->destroy) {
				info->destroy(victim);
			} else {
				delete victim;
			}
			releaseNode(*control);
		}
		reapTombstones();

		event::send<event::RequestHierarchyUpdate>();
		TOAST_INFO("World", "Removed node {} in Workspace {}", name, m_root_node->name());
		return true;
	});

	m_listener.subscribe<event::WorkspaceSave>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		auto node = findFrom(m_root_node, e.target.get());
		if (not node.exists()) {
			TOAST_WARN("World", "Tried to save workspace but the target node couldn't be found");
			return true;
		}

		// TODO: rename the root node to the file stem once SetNodeParameter exists
		assets::Prefab prefab(*node);
		auto bytes = prefab.serialize(assets::SaveMode::editor);
		if (assets::AssetManager::get().saveBytes(e.uri, bytes)) {
			TOAST_INFO("World", "Saved workspace {} to {}", node->name(), e.uri);
		}
		return true;
	});

	m_listener.subscribe<event::WorkspaceMoveNodeTo>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		TOAST_ASSERT(m_root_node.exists(), "World", "Trying to add a node to an invalid Workspace");

		auto node = findFrom(m_root_node, e.target.get());
		if (not node.exists() || node == m_root_node) {
			TOAST_WARN("World", "Tried to move node on Workspace {} but the node couldn't be found", m_root_node->name());
			return true;
		}

		// An empty new parent means "keep the current parent"
		auto dest_parent = e.new_parent.data() == 0 ? node->parentInternal() : findFrom(m_root_node, e.new_parent.get());
		if (not dest_parent.exists()) {
			TOAST_WARN("World", "Couldn't find new parent on Workspace {}", m_root_node->name());
			return true;
		}

		// Reject reparenting a node into itself or one of its descendants
		if (findFrom(node, dest_parent->uid().get()).exists()) {
			TOAST_WARN("World", "Tried to move node {} into its own descendant", node->name());
			return true;
		}

		// Detach from the current parent
		if (auto old_parent = node->parentInternal(); old_parent.exists()) {
			std::erase(old_parent->m_children, node);
		}

		node->m_parent = dest_parent;

		// Insert at the position requested by the predecessor uid
		auto& children = dest_parent->m_children;
		uint64_t pred = e.predecessor.data();
		if (pred == 0) {
			children.insert(children.begin(), node);
		} else if (pred == std::numeric_limits<uint64_t>::max()) {
			children.push_back(node);
		} else {
			auto it = std::ranges::find_if(children, [pred](const Box<Node>& c) { return c->m_uid.data() == pred; });
			if (it != children.end()) {
				children.insert(it + 1, node);
			} else {
				children.push_back(node);
			}
		}

		event::send<event::RequestHierarchyUpdate>();
		TOAST_INFO("World", "Moved node {} in Workspace {}", node->name(), m_root_node->name());
		return true;
	});

	m_listener.subscribe<event::SetFocusedNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		if (!m_root_node.exists()) {
			return false;
		}
		m_focused_node = findFrom(m_root_node, e.node.get());
		return false;
	});

	m_listener.subscribe<event::NodeChangeParam>([this](const auto& e) {
		// Only the workspace that actually owns the focused node applies the change
		if (not m_focused_node.exists()) {
			return false;
		}

		const auto* field = m_focused_node->info()->search(e.parameter);
		if (field == nullptr) {
			TOAST_WARN("World", "NodeChangeParam: unknown parameter '{}'", e.parameter);
			return true;
		}

		std::any value;
		if (field->value_type == FieldType::quaternion_t && not field->is_array) {
			// the inspector edits rotation as euler degrees "x y z"; store it back as a quaternion
			glm::vec3 deg {0.0f, 0.0f, 0.0f};
			std::istringstream ss(e.value);
			ss >> deg.x >> deg.y >> deg.z;
			value = std::any {glm::quat(glm::radians(deg))};
		} else {
			auto parsed = assets::Prefab::valueFromString(field->value_type, field->is_array, e.value);
			if (not parsed.has_value()) {
				TOAST_WARN("World", "NodeChangeParam: couldn't parse '{}' for parameter '{}'", e.value, e.parameter);
				return true;
			}
			value = std::move(*parsed);
		}

		field->set(&*m_focused_node, value);
		return true;
	});

	// Renames go through their own event so the engine is the single source of truth and can refresh
	// the hierarchy itself, rather than the editor mutating the name and forcing an update
	m_listener.subscribe<event::NodeChangeName>([this](const auto& e) {
		if (not m_root_node.exists()) {
			return false;
		}
		auto node = findFrom(m_root_node, e.node.get());
		if (not node.exists()) {
			return false;
		}
		node->m_name = e.name;
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});

	// TODO: needs function reflection for this
	// m_listener.subscribe<event::NodeCallFunction>([this](const auto& e) {
	// 	m_focused_node->info()->call(e.function);
	// });

	m_listener.subscribe<event::NodeEnabled>([this](const auto& e) {
		if (not m_root_node.exists()) {
			return false;
		}
		auto node = findFrom(m_root_node, e.node.get());
		if (not node.exists()) {
			return false;
		}
		node->enabled(e.enabled);
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});
}

void Workspace::tick() {
	// Only the active workspace streams inspector data, and only while a node is focused
	if (m_handle.data() != Engine::get()->activeWorkspace().data() || not m_focused_node.exists()) {
		m_inspector_accum = 0.0;
		return;
	}

	// Throttle to 12 fps instead of pushing the whole field set every frame
	m_inspector_accum += Time::delta();
	if (m_inspector_accum < 1.0 / 12.0) {
		return;
	}
	m_inspector_accum = 0.0;

	std::vector<event::InspectorContent::InspectorField> fields;
	Node* node = &*m_focused_node;

	for (const NodeInfo* type = node->info(); type != nullptr; type = type->base_type) {
		for (const auto& field : type->all_fields) {
			std::any value = field.get(node);

			std::string text;
			if (field.value_type == FieldType::quaternion_t && not field.is_array) {
				// rotation is exchanged with the inspector as euler degrees, not as a raw quaternion
				glm::vec3 deg = glm::degrees(glm::eulerAngles(std::any_cast<glm::quat>(value)));
				text = std::format("{} {} {}", deg.x, deg.y, deg.z);
			} else if (field.value_type == FieldType::uid_t && not field.is_array) {
				if (auto* box = std::any_cast<Box<Node>>(&value); box != nullptr) {
					text = box->exists() ? (*box)->uid().get() : "";
				} else if (auto* id = std::any_cast<UID>(&value); id != nullptr) {
					text = id->data() != 0 ? id->get() : "";
				}
			} else {
				try {
					text = assets::Prefab::stringifyValue(field.value_type, field.is_array, value);
				} catch (const std::bad_any_cast&) { text = ""; }
			}

			fields.emplace_back(field.name, text);
		}
	}

	event::send<event::InspectorContent>(
	    m_focused_node->uid().get(), m_focused_node->name(), m_focused_node->enabled(), std::move(fields)
	);
}

}
