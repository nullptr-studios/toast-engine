#include "node_owner.hpp"

#include "camera.hpp"
#include "camera_controller.hpp"
#include "node.hpp"
#include "node_3d.hpp"
#include "tree_lock.hpp"
#include "wave_executor.hpp"
#include "workspace.hpp"

#include <algorithm>
#include <charconv>
#include <future>
#include <memory>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/scripting/lua_state.hpp>
#include <toast/scripting/script_runtime.hpp>
#include <toast/thread_pool.hpp>
#include <toast/world/workspace_events.hpp>
#include <tracy/Tracy.hpp>
#include <unordered_map>

namespace toast {

void INodeOwner::updateTransforms(Node& root) {
	ZoneScoped;

	struct Walker {
		static void updateNode3D(Node3D& n3d) { n3d.syncTransform(); }

		static void walk(const Node& node) {
			if (auto* n3d = reflect_cast<Node3D>(const_cast<Node*>(&node))) {
				updateNode3D(*n3d);
			}
			for (const auto& child : node.children()) {
				walk(*child);
			}
		}
	};

	Walker::walk(root);
}

auto INodeOwner::isEditing() noexcept -> bool {
	const Workspace* workspace = asWorkspace();
	return workspace != nullptr && !workspace->isPlaying();
}

void INodeOwner::syncListenerState() noexcept {
	const bool live = receivesEvents();
	std::scoped_lock lock(nodes_mutex);
	forEachNode([live](const _detail::ControlBox& control) {
		if (control.node != nullptr && control.node->m_listener) {
			control.node->m_listener->enabled(live);
		}
	});
}

void INodeOwner::activateCamera(Camera& camera) {
	if (m_is_shutting_down || (camera.m_state != NodeState::root && camera.m_state != NodeState::global) || !camera.enabled()) {
		return;
	}

	if (m_has_camera_controller && m_active_camera_controller.exists()) {
		m_active_camera_controller->addCamera(camera);
		return;
	}

	if (m_active_camera.exists()) {
		return;
	}

	m_active_camera = camera.box().as<Camera>();
	m_active_camera->m_is_active = true;
	TOAST_INFO("World", "Active camera is now {} ({})", camera.name(), camera.uid());
	applyActiveCamera();
}

void INodeOwner::setMainCamera(Camera& camera) {
	if (m_is_shutting_down || (camera.m_state != NodeState::root && camera.m_state != NodeState::global) || !camera.enabled()) {
		return;
	}

	if (m_has_camera_controller && m_active_camera_controller.exists()) {
		m_active_camera_controller->addCamera(camera);
		m_active_camera_controller->setActiveCamera(camera.box().as<Camera>());
		return;
	}

	if (m_active_camera.exists()) {
		if (m_active_camera.rid() == camera.box().rid()) {
			return;
		}
		m_active_camera->m_is_active = false;
	}

	m_active_camera = camera.box().as<Camera>();
	m_active_camera->m_is_active = true;
	TOAST_INFO("World", "Main camera {} ({}) is now the active camera", camera.name(), camera.uid());
	applyActiveCamera();
}

void INodeOwner::deactivateCamera(Camera& camera) {
	if (m_is_shutting_down) {
		if (m_active_camera.exists() && m_active_camera.rid() == camera.box().rid()) {
			camera.m_is_active = false;
			m_active_camera = {};
		}
		return;
	}

	if (m_has_camera_controller && m_active_camera_controller.exists()) {
		m_active_camera_controller->removeCamera(camera);
		return;
	}

	if (!m_active_camera.exists() || m_active_camera.rid() != camera.box().rid()) {
		return;
	}

	camera.m_is_active = false;
	m_active_camera = {};
	findCamera();
	applyActiveCamera();
}

void INodeOwner::findCamera() {
	if (m_has_camera_controller || m_active_camera.exists()) {
		return;
	}

	// Prefer a camera flagged as main; otherwise the first live camera found
	Box<Camera> candidate;
	Box<Camera> main_candidate;
	{
		std::scoped_lock lock(nodes_mutex);
		forEachNode([&candidate, &main_candidate](const _detail::ControlBox& control) {
			if (main_candidate.exists() || control.node == nullptr || !control.node->enabled()) {
				return;
			}
			if (control.node->state() != NodeState::root && control.node->state() != NodeState::global) {
				return;
			}
			Box<Camera> camera = control.node->box().as<Camera>();
			if (!camera.exists()) {
				return;
			}
			if (camera->isMainCamera()) {
				main_candidate = camera;
			} else if (!candidate.exists()) {
				candidate = camera;
			}
		});
	}
	if (main_candidate.exists()) {
		activateCamera(*main_candidate);
	} else if (candidate.exists()) {
		activateCamera(*candidate);
	}
}

void INodeOwner::activateCameraController(CameraController& controller) {
	if (m_is_shutting_down || m_active_camera_controller.exists() ||
	    (controller.m_state != NodeState::root && controller.m_state != NodeState::global) || !controller.enabled()) {
		return;
	}

	if (m_active_camera.exists()) {
		m_active_camera->m_is_active = false;
		m_active_camera = {};
	}

	m_active_camera_controller = controller.box().as<CameraController>();
	m_has_camera_controller = true;

	std::vector<Box<Camera>> cameras;
	{
		std::scoped_lock lock(nodes_mutex);
		forEachNode([&cameras](const _detail::ControlBox& control) {
			if (control.node == nullptr || !control.node->enabled() ||
			    (control.node->state() != NodeState::root && control.node->state() != NodeState::global)) {
				return;
			}
			if (Box<Camera> camera = control.node->box().as<Camera>(); camera.exists()) {
				cameras.emplace_back(std::move(camera));
			}
		});
	}

	for (Box<Camera>& camera : cameras) {
		m_active_camera_controller->addCamera(*camera);
	}
	applyActiveCamera();
}

void INodeOwner::deactivateCameraController(CameraController& controller) {
	if (!m_active_camera_controller.exists() || m_active_camera_controller.rid() != controller.box().rid()) {
		return;
	}

	m_active_camera_controller = {};
	m_has_camera_controller = false;
	if (m_is_shutting_down) {
		return;
	}
	findCameraController();
	if (!m_has_camera_controller) {
		findCamera();
	}
	applyActiveCamera();
}

void INodeOwner::findCameraController() {
	if (m_active_camera_controller.exists()) {
		return;
	}

	Box<CameraController> candidate;
	{
		std::scoped_lock lock(nodes_mutex);
		forEachNode([&candidate](const _detail::ControlBox& control) {
			if (candidate.exists() || control.node == nullptr || !control.node->enabled() ||
			    (control.node->state() != NodeState::root && control.node->state() != NodeState::global)) {
				return;
			}
			candidate = control.node->box().as<CameraController>();
		});
	}
	if (candidate.exists()) {
		activateCameraController(*candidate);
	}
}

void INodeOwner::tickActiveCameraController() {
	if (!m_has_camera_controller || !m_active_camera_controller.exists()) {
		return;
	}
	m_active_camera_controller->callTick(m_active_camera_controller->info(), TickFunctionList::tick);
}

void INodeOwner::beginCameraShutdown() noexcept {
	m_is_shutting_down = true;
	if (m_active_camera.exists()) {
		m_active_camera->m_is_active = false;
		m_active_camera = {};
	}
	m_active_camera_controller = {};
	m_has_camera_controller = false;
}

auto INodeOwner::activeCamera() noexcept -> Box<Camera>& {
	return m_active_camera;
}

namespace {
using ControlBoxSet = std::unordered_set<_detail::ControlBox>;

// Leaked on purpose: Boxes held by queued work or Lua values can still be released during static teardown
auto& g_orphaned_control_boxes_mutex = *new std::mutex();                         // NOLINT
auto& g_orphaned_control_boxes = *new std::vector<ControlBoxSet::node_type>();    // NOLINT
}

INodeOwner::INodeOwner() = default;

INodeOwner::~INodeOwner() {
	// A Box dereferences its control block, and Lua values or C++ objects elsewhere can still hold Boxes to nodes of this
	// owner. Their control blocks have to outlive the
	// owner, so they move to a graveyard that keeps the memory until nobody references them
	std::scoped_lock lock(g_orphaned_control_boxes_mutex);
	for (auto it = nodes.begin(); it != nodes.end();) {
		if (it->ref_count.load(std::memory_order_acquire) > 0) {
			g_orphaned_control_boxes.push_back(nodes.extract(it++));
		} else {
			++it;
		}
	}
}

void INodeOwner::reapOrphanedControlBoxes() noexcept {
	std::scoped_lock lock(g_orphaned_control_boxes_mutex);
	std::erase_if(g_orphaned_control_boxes, [](const ControlBoxSet::node_type& handle) {
		return handle.value().node == nullptr && handle.value().ref_count.load(std::memory_order_acquire) == 0;
	});
}

auto INodeOwner::activeRenderCamera() noexcept -> Camera* {
	if (m_has_camera_controller) {
		if (m_active_camera_controller.exists()) {
			if (Box<Camera> camera = m_active_camera_controller->getActiveCamera(); camera.exists()) {
				return &*camera;
			}
		}
	} else if (m_active_camera.exists()) {
		return &*m_active_camera;
	}

	// Nothing in the scene to look through. Handing back null makes every consumer carry a no-camera path,
	// and the one in the renderer only ever existed on paper while the engine leaked a bootstrap camera to
	// keep it non-null. A placeholder at the origin renders the scene from 0,0,0 instead of nothing
	if (m_is_shutting_down) {
		return nullptr;
	}
	if (!m_fallback_camera) {
		m_fallback_camera = std::make_unique<Camera>();
		m_fallback_camera->syncTransform();
		TOAST_WARN("World", "No camera in the scene; rendering from a placeholder at the origin");
	}
	return m_fallback_camera.get();
}

namespace {
auto referenceUid(const assets::Prefab::BasicNode& chunk) -> uint64_t {
	if (auto field = chunk.find("m_source_prefab")) {
		try {
			return field->as<toast::UID>().data();
		} catch (const std::bad_any_cast& e) { TOAST_ERROR("World", "Bad cast at {}: {}", chunk.name, e.what()); }
	}
	return 0;
}

auto snakeToNormalCase(const std::string& text) -> std::string {
	if (text.empty()) {
		return "";
	}

	std::string result;

	for (char ch : text) {
		if (ch == '_') {
			result += ' ';
		} else {
			result += ch;
		}
	}

	// cleanup if theres trailing spaces
	if (!result.empty() && result.back() == ' ') {
		result.pop_back();
	}

	return result;
}
}

auto INodeOwner::requestRuntimeCreate(Node& parent, std::string_view type) -> Box<Node> {
	ZoneScoped;

	// Allocation
	Box node = this->nodeAllocation(type);

	// Data structure generation. Scripts create nodes on the threads that run them, so several can add children to one
	// parent at once, and others read the children meanwhile
	generateUid(node);
	{
		const TreeWriteLock lock(this);
		node->m_parent = parent;
		parent.m_children.emplace_back(node);
	}
	node->m_state = parent.m_state;
	node->m_type = NodeType::child;
	node->m_inherited_enabled = parent.enabled();
	node->m_name = uniqueChildName(parent, stripNamespace(node->m_info->type));

	// Initialization
	node->propagateCallTick(node->info(), TickFunctionList::init);
	node->propagateCallTick(node->info(), TickFunctionList::begin);
	node->m_local_enabled = true;
	node->propagateEnable();

	event::send<event::RequestHierarchyUpdate>();
	TOAST_TRACE("World", "Spawned node in {}", parent.name());
	return node;
}

auto INodeOwner::requestRuntimeSpawn(Node& parent, UID uid) -> Box<Node> {
	ZoneScoped;

	// Obtain asset
	auto file = assets::load<assets::Prefab>(uid);
	if (not file.hasValue()) {
		TOAST_ERROR("World", "Couldn't load prefab {} to spawn", uid);
		return {};
	}

	// Allocation
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = [](UID id) { return assets::load<assets::Prefab>(id); };
	seedPrefabContext(ctx, &parent);
	assets::Prefab wrapper;
	assets::Prefab::BasicNode reference = file->nodes.empty() ? assets::Prefab::BasicNode {} : file->nodes.front();
	reference.fields.clear();
	reference.groups.clear();
	reference.lua_vars.clear();
	reference.signals.clear();
	reference.fields.push_back({"m_source_prefab", FieldType::uid_t, false, uid});
	wrapper.nodes.push_back(std::move(reference));
	assets::Handle<assets::Prefab> wrapper_handle(&wrapper, UID(0), "");
	Box<Node> root = this->instantiate(wrapper_handle, ctx);
	if (not root.exists()) {
		TOAST_ERROR("World", "Failed to instantiate prefab {} to spawn", uid);
		return {};
	}

	// Data structure generation
	root->m_uid.generate();
	{
		const TreeWriteLock lock(this);
		root->m_parent = parent;
		parent.m_children.emplace_back(root);
	}
	root->m_state = parent.m_state;
	root->m_type = NodeType::root;
	root->m_inherited_enabled = parent.enabled();

	// Initialization
	root->propagateCallTick(root->info(), TickFunctionList::init);
	root->propagateCallTick(root->info(), TickFunctionList::begin);
	root->m_local_enabled = true;
	root->propagateEnable();

	event::send<event::RequestHierarchyUpdate>();
	TOAST_TRACE("World", "Spawned node {} in {}", root->name(), parent.name());
	return root;
}

auto INodeOwner::requestRuntimeSpawn(Node& parent, std::string_view uri) -> Box<Node> {
	auto uid = assets::resolveURI(uri);
	if (not uid.has_value()) {
		TOAST_WARN("World", "Couldn't find file {} to do runtime spawn", uri);
		return {};
	}

	return requestRuntimeSpawn(parent, *uid);
}

void INodeOwner::generateUid(Node& node) {
	node.m_uid.generate();
}

auto INodeOwner::stripNamespace(std::string_view type) -> std::string_view {
	auto pos = type.rfind("::");
	return pos == std::string_view::npos ? type : type.substr(pos + 2);
}

auto INodeOwner::uniqueChildName(const Node& parent, std::string_view base) -> std::string {
	const TreeReadLock lock(parent);
	auto taken = [&](std::string_view candidate) {
		return std::ranges::any_of(parent.children(), [&](const Box<Node>& c) { return c->name() == candidate; });
	};

	if (not taken(base)) {
		return std::string {base};
	}

	std::string stem {base};
	int start_n = 2;
	if (auto pos = base.rfind(' '); pos != std::string_view::npos) {
		auto suffix = base.substr(pos + 1);
		if (not suffix.empty() && std::ranges::all_of(suffix, [](char c) { return c >= '0' && c <= '9'; })) {
			stem = std::string {base.substr(0, pos)};
			start_n = std::stoi(std::string {suffix}) + 1;
		}
	}

	for (int n = start_n;; ++n) {
		auto candidate = stem + ' ' + std::to_string(n);
		if (not taken(candidate)) {
			return candidate;
		}
	}
}

auto INodeOwner::nodeAllocation(std::string_view type) noexcept -> Box<Node> {
	ZoneScoped;

	const NodeInfo* info = NodeRegistry::reflect(type);

	if (!info) {
		TOAST_WARN("World", "Reflection information for type {} not found. Falling back to toast::Node", type);
		info = NodeRegistry::reflect("toast::Node");
	}

	Node* raw_node = (info && info->construct) ? info->construct() : new Node();

	{
		std::scoped_lock lock(nodes_mutex);
		auto [it, result] = nodes.emplace(raw_node);
		TOAST_ASSERT(result, "World", "Node allocation failed");
	}
	raw_node->m_info = info;     // attach reflection data
	raw_node->m_reflect_type_name = info ? info->type : std::string_view {"toast::Node"};
	raw_node->m_owner = this;    // attach owner ptr

	return raw_node->box();
}

auto INodeOwner::nodeAllocation(const assets::Prefab::BasicNode& node_data, uint64_t script_group, bool build_scripts) noexcept
    -> Box<Node> {
	std::string type = node_data.type;
	auto box = nodeAllocation(type);
	applyFields(*box, node_data);
	box->m_script_group = script_group;
	if (build_scripts) {
		box->loadScripts();
	}
	return box;
}

void INodeOwner::applyFields(Node& node, const assets::Prefab::BasicNode& data) {
	ZoneScoped;

	std::string proper_name = snakeToNormalCase(data.name);
	node.name(proper_name);

	const NodeInfo* info = node.info();
	if (not info) {
		return;
	}

	info->forEachBaseType([&](const NodeInfo& level) {
		for (const auto& f : level.all_fields) {
			if (f.name == "m_source_prefab") {
				continue;
			}

			auto f_data = data.find(f.name);
			if (not f_data.has_value()) {
				continue;
			}

			// Read-only and transient fields are never restored from persisted data. Checking
			// NoSerialize here also keeps older files containing those fields safe to load.
			if (f.hasAttribute("ReadOnly") || f.hasAttribute("NoSerialize")) {
				continue;
			}

			if (not f.set) {
				TOAST_WARN("World", "No valid set function found for {}", f.name);
				continue;
			}

			f.set(&node, f_data->value);
		}
	});
}

void INodeOwner::applyLuaOverrides(Node& node, const assets::Prefab::BasicNode& data, const scripting::NodeResolver& find_node) {
	ZoneScoped;
	if (data.lua_vars.empty()) {
		return;
	}
	scripting::ScriptRuntime* rt = node.scriptRuntime();
	if (!rt) {
		return;
	}

	for (const auto& ov : data.lua_vars) {
		const size_t colon = ov.path.find(':');
		size_t instance = 0;
		if (colon == std::string::npos || std::from_chars(ov.path.data(), ov.path.data() + colon, instance).ec != std::errc {}) {
			TOAST_WARN("World", "Prefab lua_var override: malformed path '{}' on '{}'", ov.path, data.name);
			continue;
		}
		const std::string_view var_path = std::string_view(ov.path).substr(colon + 1);

		const scripting::ScriptSchema* schema = rt->instanceSchema(instance);
		const scripting::LuaVarDesc* desc = schema != nullptr ? schema->find(var_path) : nullptr;
		if (desc == nullptr) {
			continue;    // script no longer declares this var, skip
		}

		std::any value = scripting::parseLuaValue(*desc, ov.value, find_node);
		if (value.has_value()) {
			rt->setVarByPath(instance, var_path, value);
		} else {
			TOAST_WARN("World", "Prefab lua_var override: couldn't parse '{}' for '{}'", ov.value, ov.path);
		}
	}
}

auto INodeOwner::buildTree(std::vector<Box<Node>>&& nodes, const assets::Handle<assets::Prefab>& file) -> Box<Node> {
	ZoneScoped;

	std::unordered_map<uint64_t, Box<Node>> uid_map;
	uid_map.reserve(nodes.size());
	for (auto& node : nodes) {
		auto [it, inserted] = uid_map.emplace(node->uid().data(), node);
#ifndef NDEBUG
		if (not inserted) {
			TOAST_WARN(
			    "World",
			    "Duplicate UID {} within a single prefab ({} and {}); keeping the first",
			    node->uid(),
			    it->second->name(),
			    node->name()
			);
		}
#endif
	}

	Box<Node> root;

	// Prefab serialization guarantees exactly one rootless node, written first
	for (size_t i = 0; i < nodes.size(); ++i) {
		auto& node = nodes[i];
		const auto& data = file->nodes[i];

		auto parent_field = data.find("m_parent");
		bool has_parent = false;

		if (parent_field.has_value()) {
			try {
				UID parent_uid = parent_field->as<UID>();

				auto it = uid_map.find(parent_uid.data());
				if (it != uid_map.end()) {
					node->m_parent = it->second;
					it->second->m_children.emplace_back(node);
					has_parent = true;
				}
			} catch (const std::bad_any_cast&) {
				TOAST_WARN("World", "Cast to UID of field Parent in {} failed, treating as Root", data.name);
			}
		}

		applyLuaOverrides(*node, data, [&uid_map](std::string_view uid_text) -> Box<Node> {
			if (uid_text.empty()) {
				return {};
			}
			const uint64_t id = UID::fromString(std::string(uid_text));
			if (id == 0) {
				return {};
			}
			auto it = uid_map.find(id);
			return it != uid_map.end() ? it->second : Box<Node> {};
		});

		node->m_type = node->isInstanceRoot() || !has_parent ? NodeType::root : NodeType::child;

		if (not has_parent) {
#ifndef NDEBUG
			if (root.exists()) {
				TOAST_WARN("World", "Multiple rootless nodes in Prefab ({} and {}), keeping the last one", root->name(), data.name);
			}
#endif
			root = node;
		}
	}

	// Signals are restored after every node has been allocated and indexed, so connection targets
	// can be resolved regardless of their order in the prefab file.
	for (size_t i = 0; i < nodes.size(); ++i) {
		auto& node = nodes[i];
		const auto& data = file->nodes[i];
		const NodeInfo* info = node->info();
		if (!info) {
			continue;
		}

		for (const auto& signal_data : data.signals) {
			const SignalInfo* signal = info->getSignal(signal_data.name);
			if (!signal || !signal->connect) {
				auto* runtime = node->scriptRuntime();
				if (runtime) {
					const auto lua_names = runtime->luaSignals();
					if (std::ranges::find(lua_names, signal_data.name) != lua_names.end()) {
						for (const auto& connection : signal_data.connections) {
							auto target = uid_map.find(connection.target.data());
							if (target == uid_map.end()) {
								TOAST_WARN(
								    "World",
								    "Prefab signal '{}' on '{}' references missing UID {}",
								    signal_data.name,
								    node->name(),
								    connection.target
								);
								continue;
							}
							(void)runtime->connectLuaSignal(signal_data.name, *target->second, connection.function, true);
						}
						continue;
					}
				}
				TOAST_WARN("World", "Prefab signal '{}' is not available on node '{}'", signal_data.name, node->name());
				continue;
			}
			for (const auto& connection : signal_data.connections) {
				auto target = uid_map.find(connection.target.data());
				if (target == uid_map.end()) {
					TOAST_WARN(
					    "World", "Prefab signal '{}' on '{}' references missing UID {}", signal_data.name, node->name(), connection.target
					);
					continue;
				}
				signal->connect(&*node, *target->second, connection.function, signals::ConnectionSource::editor, true);
			}
		}
	}

#ifndef NDEBUG
	if (not root.exists()) {
		TOAST_ERROR("World", "Prefab contains no rootless node, the loaded tree has no root");
	}
#endif

	// Serialization only stores the local enabled flag; the inherited one is derived from the tree
	if (root.exists()) {
		auto propagate_inherited = [](this auto&& self, Node& n, bool inherited) -> void {
			n.m_inherited_enabled = inherited;
			for (auto& child : n.m_children) {
				self(*child, n.enabled());
			}
		};
		propagate_inherited(*root, true);
	}

	if (root.exists()) {
		TOAST_TRACE("World", "Built tree {} ({}) with {} nodes", root->name(), root->uid(), nodes.size());
	}
	return root;
}

void INodeOwner::seedPrefabContext(InstantiateContext& context, const Node* parent) const {
	if (owningPrefabUid().data() != 0) {
		context.asset_chain.push_back(owningPrefabUid().data());
	}
	const TreeReadLock lock(parent != nullptr ? parent->owner() : nullptr);
	for (const Node* n = parent; n; n = n->m_parent.exists() ? &*n->m_parent : nullptr) {
		if (n->sourcePrefab().uid().data() != 0) {
			context.asset_chain.push_back(n->sourcePrefab().uid().data());
		}
	}
}

void INodeOwner::buildLeaves(
    const assets::Handle<assets::Prefab>& file, uint64_t group, const std::vector<size_t>& leaf_slots,
    std::vector<Box<Node>>& slots
) {
	ZoneScoped;

	if (leaf_slots.empty()) {
		return;
	}

	// Whatever needs no interpreter goes to every worker. A node with scripts only gets its list here, its runtime is built
	// below. Each leaf writes its own slot, so the workers never touch the same one
	{
		ZoneScopedN("Allocate leaves");    // NOLINT
		std::vector<_detail::WaveJob> jobs(leaf_slots.size());
		for (size_t k = 0; k < jobs.size(); ++k) {
			jobs[k].items = {k};
		}
		auto allocate = [&](size_t k) {
			const size_t slot = leaf_slots[k];
			Box<Node> node = nodeAllocation(file->nodes[slot], group, false);
			if (node->m_scripts.empty()) {
				node->loadScripts();    // nothing to build, but the node hears about it like it always did
				node->callTick(node->info(), TickFunctionList::load);
				node->m_state = NodeState::loading;
			}
			slots[slot] = std::move(node);
		};
		_detail::runJobs(jobs, jobs.size(), allocate, {.non_blocking = false, .label = "Allocate leaves"});
	}

	// The scripts are placed in file order, so where they land does not depend on which worker got there first, and then
	// every interpreter builds the ones that are on it, one after the other
	std::vector<size_t> scripted;    // positions in leaf_slots
	std::vector<_detail::WaveJob> jobs;
	std::unordered_map<size_t, size_t> job_of_interpreter;
	for (size_t k = 0; k < leaf_slots.size(); ++k) {
		Node& node = *slots[leaf_slots[k]];
		if (node.m_scripts.empty()) {
			continue;
		}
		const size_t interpreter = scripting::LuaState::get().assign(group);
		node.m_script_vm = interpreter;
		const auto [job, inserted] = job_of_interpreter.try_emplace(interpreter, jobs.size());
		if (inserted) {
			jobs.push_back(_detail::WaveJob {.items = {}, .interpreters = {interpreter}});
		}
		jobs[job->second].items.push_back(scripted.size());
		scripted.push_back(k);
	}
	if (scripted.empty()) {
		return;
	}

	// The interpreter with the most nodes decides how long this takes, so it starts first
	std::ranges::stable_sort(jobs, [](const _detail::WaveJob& lhs, const _detail::WaveJob& rhs) {
		return lhs.items.size() > rhs.items.size();
	});

	ZoneNamedN(build_zone, "Build scripts", true);    // NOLINT
	auto build = [&](size_t item) {
		Node& node = *slots[leaf_slots[scripted[item]]];
		node.loadScripts();
		node.callTick(node.info(), TickFunctionList::load);
		node.m_state = NodeState::loading;
	};
	// A script that cannot be skipped has to wait for an interpreter that something outside this prefab has, so these workers
	// wait for it. They never wait for each other
	_detail::runJobs(jobs, scripted.size(), build, {.non_blocking = false, .label = "Build scripts"});
}

auto INodeOwner::instantiate(const assets::Handle<assets::Prefab>& file, InstantiateContext& ctx) -> Box<Node> {
	ZoneScoped;

	if (not file.hasValue()) {
		TOAST_ERROR("World", "Cannot instantiate an unresolved prefab {}", file.uid());
		return {};
	}

	ctx.asset_chain.push_back(file.uid().data());

	// Every node of one prefab instance runs its scripts on the same interpreter. The file being instantiated is that
	// instance, the instances nested directly in it get one of their own, and the ones nested deeper share their parent's
	const bool top_level = !ctx.nested;
	const uint64_t group = top_level ? scripting::LuaState::newGroup() : ctx.script_group;

	// deserialize + run the pre-tick lifecycle, then mark as loading
	auto alloc_leaf = [this, group](const assets::Prefab::BasicNode& chunk) -> Box<Node> {
		Box<Node> node = nodeAllocation(chunk, group);
		node->callTick(node->info(), TickFunctionList::load);
		node->m_state = NodeState::loading;
		return node;
	};

	auto make_unresolved = [&](const assets::Prefab::BasicNode& chunk, uint64_t ref_uid) -> Box<Node> {
		Box<Node> node = alloc_leaf(chunk);
		node->m_unresolved_chunk = std::make_shared<const assets::Prefab::BasicNode>(chunk);
		UID uid {ref_uid};
		std::string uri = assets::AssetManager::getURI(uid);
		node->m_source_prefab = assets::Handle<assets::Prefab>(nullptr, uid, uri);
		return node;
	};

	std::vector<Box<Node>> slots(file->nodes.size());
	std::vector<size_t> leaf_slots;

	// Leaves run Lua (loadScripts, load()). A thread that owns an interpreter must not wait for pool jobs that need
	// one, and a pool worker that waits for other workers can starve the pool, so those threads build the leaves here
	const bool build_inline = ThreadPool::onWorkerThread() || scripting::LuaState::ownsAnyState();

	for (size_t i = 0; i < file->nodes.size(); ++i) {
		const assets::Prefab::BasicNode* chunk = &file->nodes[i];
		uint64_t ref_uid = referenceUid(*chunk);

		if (ref_uid == 0) {
			if (build_inline) {
				slots[i] = alloc_leaf(*chunk);
			} else {
				leaf_slots.push_back(i);    // built together on the thread pool once the nested instances are in
			}
			continue;
		}

		// expand a nested instance synchronously on this thread
		bool cycle = std::ranges::find(ctx.asset_chain, ref_uid) != ctx.asset_chain.end();
		assets::Handle<assets::Prefab> sub = cycle ? assets::Handle<assets::Prefab> {} : ctx.resolver(toast::UID(ref_uid));

		if (cycle or not sub.hasValue()) {
			TOAST_ERROR(
			    "World",
			    "Could not instantiate nested prefab {} ({}); keeping reference unresolved",
			    toast::UID(ref_uid),
			    cycle ? "cycle detected" : "asset missing"
			);
			slots[i] = make_unresolved(*chunk, ref_uid);
			if (cycle) {
				slots[i]->m_recursive_prefab = true;
				slots[i]->addInspectorMessage({
				  .severity = NodeMessage::error,
				  .id = 0,
				  .text = "Cannot have prefab inside itself",
				});
				TOAST_ERROR("World", "Cannot have prefab inside itself");
			}
			continue;
		}

		const bool outer_nested = ctx.nested;
		const uint64_t outer_group = ctx.script_group;
		ctx.nested = true;
		ctx.script_group = top_level ? scripting::LuaState::newGroup() : group;
		Box<Node> sub_root = instantiate(sub, ctx);
		ctx.nested = outer_nested;
		ctx.script_group = outer_group;
		if (not sub_root.exists()) {
			slots[i] = make_unresolved(*chunk, ref_uid);
			continue;
		}

		applyFields(*sub_root, *chunk);
		sub_root->m_source_prefab = sub;

		// Everything below an instance root is interior to that instance
		auto mark_interior = [](this auto&& self, Node& n) -> void {
			for (auto& child : n.m_children) {
				child->m_prefab_interior = true;
				self(*child);
			}
		};
		mark_interior(*sub_root);

		slots[i] = sub_root;
	}

	buildLeaves(file, group, leaf_slots, slots);

	Box<Node> root = buildTree(std::move(slots), file);

	if (root.exists() && root->m_source_prefab.uid().data() == 0 && file.uid().data() != 0) {
		root->m_source_prefab = file;
	}

	ctx.asset_chain.pop_back();
	return root;
}

void INodeOwner::releaseNode(_detail::ControlBox& control) noexcept {
	control.node = nullptr;
	tombstones++;
}

void INodeOwner::reapTombstones() noexcept {
	if (tombstones == 0) {
		return;
	}
	std::scoped_lock lock(nodes_mutex);
	tombstones -= std::erase_if(nodes, [](const _detail::ControlBox& control) {
		return control.node == nullptr && control.ref_count.load(std::memory_order_acquire) == 0;
	});
}

void INodeOwner::reloadScriptsUsing(UID script_uid) noexcept {
	ZoneScoped;
	// Rebuilding a script replays its lifecycle, which is only safe where the game is not running
	if (!isEditing()) {
		return;
	}
	std::vector<Box<Node>> affected;
	{
		std::scoped_lock lock(nodes_mutex);
		forEachNode([&](const _detail::ControlBox& control) {
			if (control.node == nullptr) {
				return;
			}
			const bool uses_script = std::ranges::any_of(control.node->m_scripts, [&](const auto& handle) {
				return handle.uid().data() == script_uid.data();
			});
			if (uses_script) {
				affected.push_back(control.node->box());
			}
		});
	}
	// Rebuilding a runtime runs Lua, and that can allocate nodes, which takes nodes_mutex again
	for (Box<Node>& node : affected) {
		if (node.exists()) {
			node->reloadScripts();
		}
	}
}

void INodeOwner::rebindAssetHandles(UID asset_uid) noexcept {
	ZoneScoped;
	std::vector<Box<Node>> alive;
	{
		std::scoped_lock lock(nodes_mutex);
		forEachNode([&](const _detail::ControlBox& control) {
			if (control.node != nullptr) {
				alive.push_back(control.node->box());
			}
		});
	}

	for (Box<Node>& node : alive) {
		if (!node.exists() || node->info() == nullptr) {
			continue;
		}
		node->info()->forEachBaseType([&](const NodeInfo& level) {
			for (const FieldInfo& field : level.all_fields) {
				if (field.value_type != FieldType::uid_t || !field.get || !field.set || !field.type.contains("Handle<")) {
					continue;
				}
				const std::any value = field.get(&*node);
				bool uses_asset = false;
				if (!field.is_array) {
					const auto* uid = std::any_cast<UID>(&value);
					uses_asset = uid != nullptr && uid->data() == asset_uid.data();
				} else if (const auto* uids = std::any_cast<std::vector<UID>>(&value)) {
					uses_asset = std::ranges::any_of(*uids, [&](const UID& uid) { return uid.data() == asset_uid.data(); });
				}
				if (uses_asset) {
					// Assigning the uid resolves it again, which picks up the object that replaced the old one
					field.set(&*node, value);
					node->onReflectedFieldChanged(field.name);
				}
			}
		});
	}
}

void INodeOwner::refreshNodeInfos() noexcept {
	std::scoped_lock lock(nodes_mutex);
	forEachNode([](const _detail::ControlBox& control) {
		if (control.node != nullptr) {
			control.node->refreshInfo();
		}
	});
}

}
