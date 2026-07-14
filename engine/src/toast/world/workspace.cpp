#include "workspace.hpp"

#include "node.hpp"
#include "toast/engine.hpp"
#include "toast/log.hpp"
#include "workspace_events.hpp"
#include "workspace_events.pb.h"

#include <charconv>
#include <format>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <limits>
#include <memory>
#include <sstream>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/assets/prefab.hpp>
#include <toast/scripting/asset_proxy.hpp>
#include <toast/scripting/lua_types.hpp>
#include <toast/scripting/script_runtime.hpp>
#include <toast/time.hpp>

namespace toast {

struct VectorStreamBuf : std::streambuf {
	VectorStreamBuf(const std::vector<uint8_t>& vec) {
		char* p = const_cast<char*>(reinterpret_cast<const char*>(vec.data()));
		setg(p, p, p + vec.size());
	}
};

namespace {

using scripting::LuaVarDesc;
using scripting::LuaVarKind;

auto stringifyLuaValue(const LuaVarDesc& desc, const std::any& value) -> std::string {
	if (!value.has_value()) {
		return "";
	}

	if (desc.is_array) {
		auto join = [](const auto& vec, auto&& to_text, char sep = ' ') {
			std::string out;
			for (size_t i = 0; i < vec.size(); ++i) {
				if (i > 0) {
					out += sep;
				}
				out += to_text(vec[i]);
			}
			return out;
		};
		if (auto* v = std::any_cast<std::vector<bool>>(&value)) {
			std::string out;
			for (size_t i = 0; i < v->size(); ++i) {
				out += (i > 0 ? " " : "");
				out += (*v)[i] ? "true" : "false";
			}
			return out;
		}
		if (auto* v = std::any_cast<std::vector<int>>(&value)) {
			return join(*v, [](int e) { return std::format("{}", e); });
		}
		if (auto* v = std::any_cast<std::vector<double>>(&value)) {
			return join(*v, [](double e) { return std::format("{}", e); });
		}
		if (auto* v = std::any_cast<std::vector<std::string>>(&value)) {
			return join(*v, [](const std::string& e) { return e; }, '\x1f');
		}
		if (auto* v = std::any_cast<std::vector<glm::vec2>>(&value)) {
			return join(*v, [](const glm::vec2& e) { return std::format("{} {}", e.x, e.y); });
		}
		if (auto* v = std::any_cast<std::vector<glm::vec3>>(&value)) {
			return join(*v, [](const glm::vec3& e) { return std::format("{} {} {}", e.x, e.y, e.z); });
		}
		if (auto* v = std::any_cast<std::vector<glm::vec4>>(&value)) {
			return join(*v, [](const glm::vec4& e) { return std::format("{} {} {} {}", e.x, e.y, e.z, e.w); });
		}
		if (auto* v = std::any_cast<std::vector<scripting::Color3>>(&value)) {
			return join(*v, [](const scripting::Color3& e) { return std::format("{} {} {}", e.rgb.r, e.rgb.g, e.rgb.b); });
		}
		if (auto* v = std::any_cast<std::vector<scripting::Color4>>(&value)) {
			return join(*v, [](const scripting::Color4& e) {
				return std::format("{} {} {} {}", e.rgba.r, e.rgba.g, e.rgba.b, e.rgba.a);
			});
		}
		if (auto* v = std::any_cast<std::vector<Box<Node>>>(&value)) {
			return join(*v, [](const Box<Node>& e) { return e.exists() ? e->uid().get() : std::string {}; });
		}
		if (auto* v = std::any_cast<std::vector<scripting::AssetProxy>>(&value)) {
			return join(*v, [](const scripting::AssetProxy& e) { return e.uid().data() != 0 ? e.uid().get() : std::string {}; });
		}
		return "";
	}

	switch (desc.kind) {
		case LuaVarKind::boolean:
			if (auto* v = std::any_cast<bool>(&value)) {
				return *v ? "true" : "false";
			}
			break;
		case LuaVarKind::integer:
			if (auto* v = std::any_cast<int>(&value)) {
				return std::format("{}", *v);
			}
			if (auto* v = std::any_cast<double>(&value)) {
				return std::format("{}", *v);
			}
			break;
		case LuaVarKind::number:
			if (auto* v = std::any_cast<double>(&value)) {
				return std::format("{}", *v);
			}
			if (auto* v = std::any_cast<int>(&value)) {
				return std::format("{}", *v);
			}
			break;
		case LuaVarKind::string:
			if (auto* v = std::any_cast<std::string>(&value)) {
				return *v;
			}
			break;
		case LuaVarKind::vec2:
			if (auto* v = std::any_cast<glm::vec2>(&value)) {
				return std::format("{} {}", v->x, v->y);
			}
			break;
		case LuaVarKind::vec3:
			if (auto* v = std::any_cast<glm::vec3>(&value)) {
				return std::format("{} {} {}", v->x, v->y, v->z);
			}
			break;
		case LuaVarKind::vec4:
			if (auto* v = std::any_cast<glm::vec4>(&value)) {
				return std::format("{} {} {} {}", v->x, v->y, v->z, v->w);
			}
			break;
		case LuaVarKind::color3:
			if (auto* v = std::any_cast<scripting::Color3>(&value)) {
				return std::format("{} {} {}", v->rgb.r, v->rgb.g, v->rgb.b);
			}
			break;
		case LuaVarKind::color4:
			if (auto* v = std::any_cast<scripting::Color4>(&value)) {
				return std::format("{} {} {} {}", v->rgba.r, v->rgba.g, v->rgba.b, v->rgba.a);
			}
			break;
		case LuaVarKind::node_ref:
			if (auto* v = std::any_cast<Box<Node>>(&value)) {
				return v->exists() ? (*v)->uid().get() : "";
			}
			break;
		case LuaVarKind::asset_ref:
			if (auto* v = std::any_cast<scripting::AssetProxy>(&value)) {
				return v->uid().data() != 0 ? v->uid().get() : "";
			}
			break;
	}
	return "";
}

auto parseFloats(std::string_view text) -> std::vector<float> {
	std::vector<float> out;
	std::istringstream ss {std::string(text)};
	float f = 0.0f;
	while (ss >> f) {
		out.push_back(f);
	}
	return out;
}

auto parseLuaValue(
    const LuaVarDesc& desc, std::string_view text, const std::function<toast::Box<Node>(std::string_view)>& find_node
) -> std::any {
	auto parse_uid = [](std::string_view s) { return s.empty() ? toast::UID {0} : toast::UID::fromString(std::string(s)); };

	if (desc.is_array) {
		const std::vector<float> floats = parseFloats(text);
		auto group = [&](size_t stride, auto&& make) {
			using T = decltype(make(0));
			std::vector<T> out;
			for (size_t i = 0; i + stride <= floats.size(); i += stride) {
				out.push_back(make(i));
			}
			return std::any {std::move(out)};
		};
		auto tokens = [&](char sep) {
			std::vector<std::string> out;
			size_t start = 0;
			while (start <= text.size()) {
				const size_t end = text.find(sep, start);
				const auto piece = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
				if (!piece.empty() || sep == '\x1f') {
					out.emplace_back(piece);
				}
				if (end == std::string_view::npos) {
					break;
				}
				start = end + 1;
			}
			return out;
		};

		switch (desc.kind) {
			case LuaVarKind::boolean: {
				std::vector<bool> out;
				for (const auto& t : tokens(' ')) {
					out.push_back(t == "true");
				}
				return out;
			}
			case LuaVarKind::integer: {
				std::vector<int> out;
				out.reserve(floats.size());
				for (float f : floats) {
					out.push_back(static_cast<int>(f));
				}
				return out;
			}
			case LuaVarKind::number: {
				std::vector<double> out;
				out.reserve(floats.size());
				for (float f : floats) {
					out.push_back(f);
				}
				return out;
			}
			case LuaVarKind::string: return text.empty() ? std::any {std::vector<std::string> {}} : std::any {tokens('\x1f')};
			case LuaVarKind::vec2: return group(2, [&](size_t i) { return glm::vec2(floats[i], floats[i + 1]); });
			case LuaVarKind::vec3: return group(3, [&](size_t i) { return glm::vec3(floats[i], floats[i + 1], floats[i + 2]); });
			case LuaVarKind::vec4:
				return group(4, [&](size_t i) { return glm::vec4(floats[i], floats[i + 1], floats[i + 2], floats[i + 3]); });
			case LuaVarKind::color3:
				return group(3, [&](size_t i) { return scripting::Color3(floats[i], floats[i + 1], floats[i + 2]); });
			case LuaVarKind::color4:
				return group(4, [&](size_t i) { return scripting::Color4(floats[i], floats[i + 1], floats[i + 2], floats[i + 3]); });
			case LuaVarKind::node_ref: {
				std::vector<toast::Box<Node>> out;
				for (const auto& t : tokens(' ')) {
					out.push_back(find_node(t));
				}
				return out;
			}
			case LuaVarKind::asset_ref: {
				std::vector<scripting::AssetProxy> out;
				for (const auto& t : tokens(' ')) {
					const toast::UID uid = parse_uid(t);
					out.push_back(uid.data() != 0 ? scripting::AssetProxy(uid) : scripting::AssetProxy(assets::AssetHandleBase(nullptr)));
				}
				return out;
			}
		}
		return {};
	}

	switch (desc.kind) {
		case LuaVarKind::boolean: return text == "true";
		case LuaVarKind::integer: {
			int v = 0;
			auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
			return ec == std::errc {} ? std::any {v} : std::any {};
		}
		case LuaVarKind::number: {
			const std::vector<float> f = parseFloats(text);
			return f.empty() ? std::any {} : std::any {static_cast<double>(f[0])};
		}
		case LuaVarKind::string: return std::string(text);
		case LuaVarKind::vec2: {
			const auto f = parseFloats(text);
			return f.size() >= 2 ? std::any {glm::vec2(f[0], f[1])} : std::any {};
		}
		case LuaVarKind::vec3: {
			const auto f = parseFloats(text);
			return f.size() >= 3 ? std::any {glm::vec3(f[0], f[1], f[2])} : std::any {};
		}
		case LuaVarKind::vec4: {
			const auto f = parseFloats(text);
			return f.size() >= 4 ? std::any {glm::vec4(f[0], f[1], f[2], f[3])} : std::any {};
		}
		case LuaVarKind::color3: {
			const auto f = parseFloats(text);
			return f.size() >= 3 ? std::any {scripting::Color3(f[0], f[1], f[2])} : std::any {};
		}
		case LuaVarKind::color4: {
			const auto f = parseFloats(text);
			return f.size() >= 4 ? std::any {scripting::Color4(f[0], f[1], f[2], f[3])} : std::any {};
		}
		case LuaVarKind::node_ref: return find_node(text);
		case LuaVarKind::asset_ref: {
			const toast::UID uid = parse_uid(text);
			if (uid.data() == 0) {
				return scripting::AssetProxy(assets::AssetHandleBase(nullptr));
			}
			return scripting::AssetProxy(uid);
		}
	}
	return {};
}

}

static std::unique_ptr<assets::Prefab> s_clipboard;

Workspace::Workspace(UID handle, EmptyTag) : m_handle(handle) {
	eventSubscriptions();
}

Workspace::Workspace(std::string_view type, UID handle) : m_handle(handle) {
	ZoneScoped;
	eventSubscriptions();

	// Allocation
	Box node = this->nodeAllocation(type);
	node->propagateCallTick(node->info(), TickFunctionList::pre_init);
	node->propagateCallTick(node->info(), TickFunctionList::load);

	// Data structure generation
	generateUid(node);
	node->m_parent = {};
	node->m_state = NodeState::root;
	node->m_type = NodeType::world_root;
	node->m_inherited_enabled = true;
	node->m_name = stripNamespace(node->info()->type);

	// Initialization
	node->propagateCallTick(node->info(), TickFunctionList::init);
	node->propagateCallTick(node->info(), TickFunctionList::begin);
	node->m_local_enabled = true;
	node->propagateEnable();

	m_root_node = node;
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

	initFromPrefab(file);
	if (m_root_node.exists()) {
		TOAST_INFO("World", "Opened workspace from {}", uid);
	}
}

Workspace::Workspace(UID uid, std::string_view source_uri) : m_handle(uid) {
	eventSubscriptions();

	auto bytes = assets::AssetManager::get().loadBytes(source_uri);
	if (not bytes.has_value()) {
		TOAST_ERROR("World", "Couldn't open workspace source file {}", source_uri);
		return;
	}

	// We need this because if we use the vector<uint8> constructor is going to try
	// to load the node as a .tbnode rather than as a .tnode, we need to be careful with that
	VectorStreamBuf buffer(*bytes);
	std::istream autosave(&buffer);
	assets::Prefab prefab(autosave);
	assets::AssetHandle<assets::Prefab> file(&prefab, uid, "");
	initFromPrefab(file);
	if (m_root_node.exists()) {
		TOAST_INFO("World", "Opened workspace {} from {}", uid, source_uri);
	}
}

void Workspace::initFromPrefab(const assets::AssetHandle<assets::Prefab>& file) {
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
	Box<Node> node = instantiate(file, ctx);
	if (not node.exists()) {
		TOAST_ERROR("World", "Failed to instantiate node {}", m_handle);
		return;
	}
	// node->propagateCallTick(node->info(), TickFunctionList::load);
	// node->propagateCallTick(node->info(), TickFunctionList::pre_init);

	// Data structure generation
	generateUid(node);
	node->m_parent = {};
	node->m_state = NodeState::root;
	node->m_type = NodeType::world_root;
	node->m_inherited_enabled = true;

	// Initialization
	node->propagateCallTick(node->info(), TickFunctionList::init);
	node->propagateCallTick(node->info(), TickFunctionList::begin);
	node->m_local_enabled = true;
	node->propagateEnable();

	m_root_node = node;
}

Workspace::~Workspace() {
	if (!m_root_node.exists()) {
		return;
	}

	m_root_node->propagateCallTick(m_root_node->info(), TickFunctionList::on_disable);
	m_root_node->propagateCallTick(m_root_node->info(), TickFunctionList::end);
	m_root_node->propagateCallTick(m_root_node->info(), TickFunctionList::destroy);

	std::vector<Node*> victims;
	auto collect = [&victims](this auto&& self, Node& n) -> void {
		victims.push_back(&n);
		for (auto& c : n.m_children) {
			self(*c);
		}
	};
	collect(*m_root_node);
	m_root_node = {};
	m_focused_node = {};

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
}

auto Workspace::name() -> std::string {
	if (!m_root_node.exists()) {
		return "";
	}
	return std::string {m_root_node->name()};
}

void Workspace::registerDependency(Node& from, Node& to) {
	// TODO:
	// TOAST_NOT_IMPLEMENTED;
}

void Workspace::unregisterDependency(Node& from, Node& to) { }

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

		event::send<event::ReloadAssetsManifest>();
		return true;
	});

	// Unlike WorkspaceSave this matches on the workspace handle, so background
	// workspaces autosave too, and it never touches the asset manifest
	m_listener.subscribe<event::WorkspaceAutosave>([this](const auto& e) {
		if (e.handle != m_handle.data()) {
			return false;
		}
		if (not m_root_node.exists()) {
			return true;
		}

		assets::Prefab prefab(*m_root_node);
		auto bytes = prefab.serialize(assets::SaveMode::editor);
		if (assets::AssetManager::get().saveBytes(e.uri, bytes)) {
			TOAST_INFO("World", "Autosaved workspace {} to {}", m_root_node->name(), e.uri);
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

		if (field->name == "m_scripts") {
			m_focused_node->loadScripts();
			event::send<event::RequestHierarchyUpdate>();
		}
		return true;
	});

	// Lua variable edits address "<instance>:group/subgroup/name" through the script schema
	m_listener.subscribe<event::NodeChangeLuaParam>([this](const auto& e) {
		if (not m_focused_node.exists()) {
			return false;
		}
		scripting::ScriptRuntime* rt = m_focused_node->scriptRuntime();
		if (rt == nullptr) {
			return true;
		}

		const size_t colon = e.path.find(':');
		size_t instance = 0;
		if (colon == std::string::npos || std::from_chars(e.path.data(), e.path.data() + colon, instance).ec != std::errc {}) {
			TOAST_WARN("World", "NodeChangeLuaParam: malformed path '{}'", e.path);
			return true;
		}
		const auto var_path = std::string_view(e.path).substr(colon + 1);

		const scripting::ScriptSchema* schema = rt->instanceSchema(instance);
		const LuaVarDesc* desc = schema != nullptr ? schema->find(var_path) : nullptr;
		if (desc == nullptr) {
			TOAST_WARN("World", "NodeChangeLuaParam: unknown variable '{}'", e.path);
			return true;
		}

		auto find_node = [this](std::string_view uid_text) -> Box<Node> {
			if (uid_text.empty() || toast::UID::fromString(std::string(uid_text)) == 0) {
				return {};
			}
			return findFrom(m_root_node, uid_text);
		};

		std::any value = parseLuaValue(*desc, e.value, find_node);
		if (value.has_value()) {
			rt->setVarByPath(instance, var_path, value);
		} else {
			TOAST_WARN("World", "NodeChangeLuaParam: couldn't parse '{}' for '{}'", e.value, e.path);
		}
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

	m_listener.subscribe<event::WorkspaceSpawn>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		TOAST_ASSERT(m_root_node.exists(), "World", "Trying to spawn a node in an invalid Workspace");

		auto parent = findFrom(m_root_node, e.parent.get());
		if (not parent.exists()) {
			TOAST_WARN("World", "WorkspaceSpawn: parent {} not found", e.parent);
			return true;
		}

		// requestRuntimeSpawn sends RequestHierarchyUpdate on success
		if (e.is_uri) {
			requestRuntimeSpawn(parent, e.uri);
		} else {
			requestRuntimeSpawn(parent, e.uid);
		}
		return true;
	});

	m_listener.subscribe<event::WorkspaceDuplicateNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}

		auto src = findFrom(m_root_node, e.source.get());
		auto par = findFrom(m_root_node, e.parent.get());
		if (not src.exists() || not par.exists()) {
			TOAST_WARN("World", "WorkspaceDuplicateNode: source or parent not found");
			return true;
		}

		assets::Prefab prefab(*src);
		assets::AssetHandle<assets::Prefab> handle(&prefab, toast::UID(0), "");

		InstantiateContext ctx;
		ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
		Box<Node> copy = instantiate(handle, ctx);
		if (not copy.exists()) {
			TOAST_WARN("World", "WorkspaceDuplicateNode: instantiation failed");
			return true;
		}

		// Regenerate UIDs on every node in the copy to avoid collisions with the originals
		auto regen = [&](auto& self, Box<Node>& node) -> void {
			INodeOwner::generateUid(node);
			for (auto& child : node->m_children) {
				self(self, child);
			}
		};
		regen(regen, copy);

		// The in-memory prefab has no meaningful UID, so clear the root's source_prefab
		// to avoid marking the copy as a prefab instance with UID 0
		copy->m_source_prefab = {};

		// Ensure the copy doesn't share its name with an existing sibling
		copy->m_name = uniqueChildName(*par, copy->name());

		copy->m_parent = par;
		par->m_children.emplace_back(copy);
		copy->m_state = par->m_state;
		copy->m_type = NodeType::child;
		copy->m_inherited_enabled = par->enabled();

		copy->propagateCallTick(copy->info(), TickFunctionList::init);
		copy->propagateCallTick(copy->info(), TickFunctionList::begin);
		copy->enabled(true);

		event::send<event::RequestHierarchyUpdate>();
		TOAST_INFO("World", "Duplicated {} under {}", src->name(), par->name());
		return true;
	});

	m_listener.subscribe<event::NodeChangeType>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}

		auto target = findFrom(m_root_node, e.node.get());
		if (not target.exists()) {
			TOAST_WARN("World", "NodeChangeType: node not found");
			return true;
		}

		auto parent = target->parentInternal();
		if (not parent.exists()) {
			TOAST_WARN("World", "NodeChangeType: cannot change type of root node");
			return true;
		}

		// Allocate the replacement node
		Box<Node> fresh = nodeAllocation(e.type);
		fresh->propagateCallTick(fresh->info(), TickFunctionList::pre_init);

		fresh->m_uid = target->m_uid;
		fresh->m_name = target->m_name;
		fresh->m_state = target->m_state;
		fresh->m_type = target->m_type;
		fresh->m_inherited_enabled = target->m_inherited_enabled;

		// Transfer children from the old node to the new one
		for (auto& child : target->m_children) {
			child->m_parent = fresh;
			fresh->m_children.push_back(std::move(child));
		}
		target->m_children.clear();

		// Replace the old node in the parent's children list
		fresh->m_parent = parent;
		auto& siblings = parent->m_children;
		auto it = std::ranges::find(siblings, target);
		if (it != siblings.end()) {
			*it = fresh;
		}

		// Destroy the old node using the same pattern as WorkspaceRemoveNode
		Node* old_raw = &*target;
		_detail::ControlBox* old_ctrl = _detail::ControlBox::get(old_raw);
		const NodeInfo* old_info = old_raw->info();
		old_raw->m_parent = {};
		old_raw->m_listener.reset();
		target = {};

		if (old_info && old_info->destroy) {
			old_info->destroy(old_raw);
		} else {
			delete old_raw;
		}
		releaseNode(*old_ctrl);
		reapTombstones();

		// Initialize the fresh node
		fresh->callTick(fresh->info(), TickFunctionList::init);
		fresh->callTick(fresh->info(), TickFunctionList::begin);
		fresh->enabled(true);

		event::send<event::RequestHierarchyUpdate>();
		TOAST_INFO("World", "Changed node type to {}", e.type);
		return true;
	});

	// C# side creates the empty file + meta + rebuilds the asset database before sending this
	// Here we write the actual node content and replace the inline subtree with a prefab reference
	// Asset creation is becoming an interconnected mess of c++ and c# and im starting to get mad -x
	m_listener.subscribe<event::WorkspacePromoteNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}

		auto target = findFrom(m_root_node, e.target.get());
		if (not target.exists()) {
			TOAST_WARN("World", "WorkspacePromoteNode: target not found");
			return true;
		}

		auto parent = target->parentInternal();
		if (not parent.exists()) {
			TOAST_WARN("World", "WorkspacePromoteNode: cannot promote root node");
			return true;
		}

		// Write the node content to the file the C# side already created on disk
		assets::Prefab prefab(*target);
		auto bytes = prefab.serialize(assets::SaveMode::editor);
		assets::AssetManager::get().saveBytes(e.path, bytes);

		std::erase(parent->m_children, target);

		std::vector<Node*> victims;
		auto collect = [&victims](this auto&& self, Node& n) -> void {
			victims.push_back(&n);
			for (auto& c : n.m_children) {
				self(*c);
			}
		};
		collect(*target);
		target = {};

		for (Node* victim : victims) {
			_detail::ControlBox* ctrl = _detail::ControlBox::get(victim);
			const NodeInfo* info = victim->info();
			victim->m_parent = {};
			victim->m_children.clear();
			victim->m_listener.reset();
			if (info && info->destroy) {
				info->destroy(victim);
			} else {
				delete victim;
			}
			releaseNode(*ctrl);
		}
		reapTombstones();

		// Spawn the saved file as a prefab child of the same parent
		auto uid = assets::resolveURI(e.path);
		if (uid.has_value()) {
			requestRuntimeSpawn(parent, *uid);
		} else {
			TOAST_WARN("World", "WorkspacePromoteNode: couldn't resolve UID for {}", e.path);
			event::send<event::RequestHierarchyUpdate>();
		}

		TOAST_INFO("World", "Promoted node to {}", e.path);
		return true;
	});

	// mirrored editor toolbar state; the active workspace is the one being edited
	m_listener.subscribe<event::SetGizmoTool>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		m_gizmo_tool = static_cast<GizmoTool>(e.tool);
		return true;
	});

	m_listener.subscribe<event::SetCoordinateSpace>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		m_world_space = e.world;
		return true;
	});

	m_listener.subscribe<event::SetSnapping>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		SnapSetting setting {e.enabled, e.value};
		switch (e.kind) {
			case 0: m_translate_snap = setting; break;
			case 1: m_rotate_snap = setting; break;
			case 2: m_scale_snap = setting; break;
			default: TOAST_WARN("World", "SetSnapping: unknown kind {}", e.kind); break;
		}
		return true;
	});

	m_listener.subscribe<event::SetCameraMode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		m_game_camera = e.game;
		return true;
	});

	m_listener.subscribe<event::WorkspaceCopyNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		auto src = findFrom(m_root_node, e.source.get());
		if (not src.exists()) {
			TOAST_WARN("World", "WorkspaceCopyNode: source not found");
			return true;
		}
		s_clipboard = std::make_unique<assets::Prefab>(*src);
		return true;
	});

	m_listener.subscribe<event::WorkspacePasteNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		if (not s_clipboard) {
			return true;
		}
		auto par = findFrom(m_root_node, e.parent.get());
		if (not par.exists()) {
			TOAST_WARN("World", "WorkspacePasteNode: parent not found");
			return true;
		}

		assets::AssetHandle<assets::Prefab> handle(s_clipboard.get(), toast::UID(0), "");
		InstantiateContext ctx;
		ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
		Box<Node> copy = instantiate(handle, ctx);
		if (not copy.exists()) {
			TOAST_WARN("World", "WorkspacePasteNode: instantiation failed");
			return true;
		}

		auto regen = [&](auto& self, Box<Node>& node) -> void {
			INodeOwner::generateUid(node);
			for (auto& child : node->m_children) {
				self(self, child);
			}
		};
		regen(regen, copy);

		copy->m_source_prefab = {};
		copy->m_name = uniqueChildName(*par, copy->name());
		copy->m_parent = par;
		par->m_children.emplace_back(copy);
		copy->m_state = par->m_state;
		copy->m_type = NodeType::child;
		copy->m_inherited_enabled = par->enabled();

		copy->propagateCallTick(copy->info(), TickFunctionList::init);
		copy->propagateCallTick(copy->info(), TickFunctionList::begin);
		copy->enabled(true);

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

	// The exported script variables travel in their own message so the editor can rebuild
	// its Lua cards independently of the reflected C++ structure
	if (scripting::ScriptRuntime* rt = node->scriptRuntime()) {
		std::vector<event::InspectorLuaContent::LuaScriptCard> cards;
		cards.reserve(rt->instanceCount());

		for (size_t i = 0; i < rt->instanceCount(); ++i) {
			const scripting::ScriptSchema* schema = rt->instanceSchema(i);
			if (schema == nullptr) {
				continue;
			}

			auto make_field = [&](const LuaVarDesc& d) {
				event::InspectorLuaContent::LuaField f;
				f.path = std::format("{}:{}", i, d.path);
				f.name = d.name;
				f.kind = static_cast<uint32_t>(d.kind);
				f.is_array = d.is_array;
				f.ref_type = d.ref_type;
				f.value = stringifyLuaValue(d, rt->getVarByPath(i, d.path));
				return f;
			};

			event::InspectorLuaContent::LuaScriptCard card;
			card.script = rt->instanceScript(i);
			for (const auto& d : schema->fields) {
				card.fields.push_back(make_field(d));
			}
			for (const auto& g : schema->groups) {
				event::InspectorLuaContent::LuaGroup group;
				group.name = g.name;
				for (const auto& d : g.fields) {
					group.fields.push_back(make_field(d));
				}
				for (const auto& sg : g.subgroups) {
					event::InspectorLuaContent::LuaSubgroup sub;
					sub.name = sg.name;
					for (const auto& d : sg.fields) {
						sub.fields.push_back(make_field(d));
					}
					group.subgroups.push_back(std::move(sub));
				}
				card.groups.push_back(std::move(group));
			}
			cards.push_back(std::move(card));
		}

		if (!cards.empty()) {
			event::send<event::InspectorLuaContent>(m_focused_node->uid().get(), rt->schemaVersion(), std::move(cards));
		}
	}
}

}
