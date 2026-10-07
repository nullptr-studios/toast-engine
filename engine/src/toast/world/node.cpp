#include "node.hpp"

#include "tree_lock.hpp"
#include "world.hpp"

#include <toast/scripting/lua_state.hpp>
#include <toast/scripting/script_runtime.hpp>

namespace toast::_detail {

void callNodeScripts(Node* node, std::string_view name, std::span<const std::any> args) noexcept {
	if (auto* rt = node->scriptRuntime()) {
		rt->callWithAnyArgs(name, args);
	}
}

void setNodeScriptVar(Node* node, std::string_view name, const std::any& value) noexcept {
	if (auto* rt = node->scriptRuntime()) {
		rt->setVar(name, value);
	}
}

auto getNodeScriptVar(const Node* node, std::string_view name) noexcept -> std::any {
	if (auto* rt = const_cast<Node*>(node)->scriptRuntime()) {
		return rt->getVar(name);
	}
	return {};
}

}

namespace toast {

Node::Node() = default;
Node::~Node() = default;

auto Node::uid() const noexcept -> const UID& {
	return m_uid;
}

auto Node::name() const noexcept -> std::string_view {
	return m_name;
}

void Node::name(std::string_view name) noexcept {
	m_name = name;
}

auto Node::enabled() const noexcept -> bool {
	return m_local_enabled && m_inherited_enabled;
}

void Node::enabled(bool value) noexcept {
	ZoneScoped;

	if (m_local_enabled == value) {
		return;
	}
	if (not m_inherited_enabled) {
		return;
	}
	m_local_enabled = value;
	TOAST_TRACE("Node", "{} ({}) {}", name(), uid(), value ? "enabled" : "disabled");
	if (m_owner != nullptr) {
		m_owner->nodeEnabledChanged();
	}

	if (value) {
		callTick(m_info, TickFunctionList::on_enable);
	} else {
		callTick(m_info, TickFunctionList::on_disable);
	}

	for (Box<Node> c : childrenSnapshot()) {
		if (c.exists()) {
			c->inheritedEnabled(value);
		}
	}
}

auto Node::box() const noexcept -> Box<Node> {
	return m_box;
}

auto Node::parent() noexcept -> Box<Node> {
	Box<Node> result;
	{
		const TreeReadLock lock(*this);
		result = m_parent;
	}
	if (result.exists()) {
		m_owner->registerDependency(*result, *this);
	}
	return result;
}

auto Node::childrenSnapshot() const -> std::vector<Box<Node>> {
	const TreeReadLock lock(*this);
	return m_children;
}

auto Node::info() const -> const NodeInfo* {
	return m_info;
}

void Node::refreshInfo() {
	const NodeInfo* current = NodeRegistry::reflect(m_reflect_type_name);
	if (current && current != m_info) {
		m_info = current;
	}
}

auto Node::sourcePrefab() const noexcept -> const assets::Handle<assets::Prefab>& {
	return m_source_prefab;
}

auto Node::isInstanceRoot() const noexcept -> bool {
	return m_source_prefab.uid().data() != 0;
}

auto Node::root() const noexcept -> Box<Node> {
	const TreeReadLock lock(*this);
	return rootUnlocked();
}

auto Node::rootUnlocked() const noexcept -> Box<Node> {
	const Node* n = this;
	while (true) {
		if (n->isInstanceRoot() || not n->m_parent.exists()) {
			return n->box();
		}
		n = &*n->m_parent;
	}
}

auto Node::find(std::string_view query) -> Box<Node> {
	if (not m_owner) {
		return {};
	}
	return m_owner->findFrom(*this, query);
}

auto Node::find(const UID& uid) -> Box<Node> {
	if (not m_owner) {
		return {};
	}
	return m_owner->findFrom(*this, uid);
}

auto Node::search(std::string_view query) -> std::vector<Box<Node>> {
	if (not m_owner) {
		return {};
	}
	return m_owner->searchFrom(*this, query);
}

auto Node::childrenOfType(const NodeInfo* type) const -> std::vector<Box<Node>> {
	std::vector<Box<Node>> out;
	if (not type) {
		return out;
	}
	const TreeReadLock lock(*this);
	for (const auto& c : m_children) {
		if (c.exists() and c->info() and c->info()->isA(type)) {
			out.push_back(c);
		}
	}
	return out;
}

auto Node::searchType(const NodeInfo* type) const -> std::vector<Box<Node>> {
	std::vector<Box<Node>> out;
	if (not type) {
		return out;
	}
	const TreeReadLock lock(*this);
	auto dfs = [&](this auto&& self, const Node& n) -> void {
		for (const auto& c : n.m_children) {
			if (not c.exists()) {
				continue;
			}
			if (c->info() and c->info()->isA(type)) {
				out.push_back(c);
			}
			self(*c);
		}
	};
	dfs(*this);
	return out;
}

void Node::addDependsOn(Node& other) {
	m_owner->registerDependency(other, *this);
}

void Node::removeDependsOn(Node& other) {
	m_owner->unregisterDependency(other, *this);
}

auto Node::listener() noexcept -> event::Listener& {
	if (not m_listener) {
		m_listener = std::make_unique<event::Listener>();
		TOAST_TRACE("Node", "Created listener for {} ({})", name(), uid());
	}

	return *m_listener;
}

void Node::inheritedEnabled(bool value) noexcept {
	ZoneScoped;

	if (m_inherited_enabled == value) {
		return;
	}
	m_inherited_enabled = value;

	if (m_local_enabled) {
		if (value) {
			callTick(m_info, TickFunctionList::on_enable);
		} else {
			callTick(m_info, TickFunctionList::on_disable);
		}
	}

	for (Box<Node> c : childrenSnapshot()) {
		if (c.exists()) {
			c->inheritedEnabled(value);
		}
	}
}

void Node::changeNodeState(NodeState state) noexcept {
	m_state = state;
	for (Box<Node> c : childrenSnapshot()) {
		if (c.exists()) {
			c->changeNodeState(state);
		}
	}
}

auto Node::hasTickFunction(TickFunctionList mask) const noexcept -> bool {
	if (m_info && m_info->hasFunction(mask)) {
		return true;
	}
	return m_script_runtime && m_script_runtime->hasTick(mask);
}

auto Node::scriptVm(TickFunctionList func) const noexcept -> std::optional<size_t> {
	if (m_script_runtime && m_script_runtime->hasTick(func)) {
		return m_script_runtime->stateIndex();
	}
	return luaAffinity();
}

auto Node::scriptVmAny() const noexcept -> std::optional<size_t> {
	if (m_script_runtime && m_script_runtime->instanceCount() > 0) {
		return m_script_runtime->stateIndex();
	}
	return luaAffinity();
}

void Node::interactsWith(Node& other) {
	if (m_owner != nullptr) {
		m_owner->registerInteraction(*this, other);
	}
}

auto Node::hasCallable(std::string_view callable_name) const noexcept -> bool {
	if (m_info && m_info->getMethod(callable_name)) {
		return true;
	}
	return m_script_runtime && m_script_runtime->hasFunction(callable_name);
}

void Node::loadScripts() noexcept {
	onScriptsReloading();
	// A runtime that is executing right now must not be freed under its own frames
	scripting::ScriptRuntime::retire(std::move(m_script_runtime));
	m_script_runtime.reset();
	const std::optional<size_t> placed = std::exchange(m_script_vm, std::nullopt);
	if (m_scripts.empty()) {
		if (placed.has_value() && scripting::LuaState::exists()) {
			scripting::LuaState::get().unassign(m_script_group, *placed);
		}
		return;
	}
	m_script_runtime = std::make_unique<scripting::ScriptRuntime>(m_box, m_scripts, m_script_group, placed);
}

void Node::reloadScripts() noexcept {
	ZoneScopedN("Lua reload");    // NOLINT
	using scripting::LuaVarDesc;

	struct SavedVar {
		std::string path;
		scripting::LuaVarKind kind;
		bool is_array;
		std::any value;
	};

	struct SavedLuaSignal {
		std::string signal;
		UID target;
		std::string function;
		bool forwards_args = true;
	};

	std::vector<std::vector<SavedVar>> saved;
	std::vector<SavedLuaSignal> editor_signals;
	const uint8_t reached = m_lifecycle;

	if (m_script_runtime) {
		if ((reached & lifecycle_enabled) != 0 && enabled()) {
			m_script_runtime->call(TickFunctionList::on_disable);
		}
		if ((reached & lifecycle_begun) != 0) {
			m_script_runtime->call(TickFunctionList::end);
		}
		if ((reached & lifecycle_initialized) != 0) {
			m_script_runtime->call(TickFunctionList::destroy);
		}

		for (const std::string& signal_name : m_script_runtime->luaSignals()) {
			for (const signals::ConnectionInfo& connection : m_script_runtime->luaSignalConnections(signal_name)) {
				if (connection.source == signals::ConnectionSource::editor) {
					editor_signals.push_back(
					    {.signal = signal_name,
							 .target = connection.target,
							 .function = connection.function,
							 .forwards_args = connection.forwards_args}
					);
				}
			}
		}
	}

	if (m_script_runtime) {
		saved.resize(m_script_runtime->instanceCount());
		for (size_t i = 0; i < m_script_runtime->instanceCount(); ++i) {
			const scripting::ScriptSchema* schema = m_script_runtime->instanceSchema(i);
			if (schema == nullptr) {
				continue;
			}
			schema->forEach([&](const LuaVarDesc& d) {
				std::any value = m_script_runtime->getVarByPath(i, d.path);
				if (value.has_value()) {
					saved[i].push_back({d.path, d.kind, d.is_array, std::move(value)});
				}
			});
		}
	}

	loadScripts();

	// Restore unchanged values
	size_t preserved = 0;
	if (m_script_runtime) {
		const size_t shared = std::min(saved.size(), m_script_runtime->instanceCount());
		for (size_t i = 0; i < shared; ++i) {
			const scripting::ScriptSchema* schema = m_script_runtime->instanceSchema(i);
			if (schema == nullptr) {
				continue;
			}
			for (const SavedVar& var : saved[i]) {
				const LuaVarDesc* desc = schema->find(var.path);
				if (desc != nullptr && desc->kind == var.kind && desc->is_array == var.is_array &&
				    m_script_runtime->setVarByPath(i, var.path, var.value)) {
					++preserved;
				}
			}
		}
	}

	// Connections made in the editor to signals the scripts declare died with the old runtime
	if (m_script_runtime && m_owner != nullptr) {
		for (const SavedLuaSignal& saved_signal : editor_signals) {
			if (Box<Node> target = find(saved_signal.target); target.exists()) {
				m_script_runtime->connectLuaSignal(saved_signal.signal, *target, saved_signal.function, saved_signal.forwards_args);
			}
		}
	}

	// Bring the new scripts to the point the node is at
	if (m_script_runtime) {
		if ((reached & lifecycle_loaded) != 0) {
			m_script_runtime->call(TickFunctionList::load);
		}
		if ((reached & lifecycle_initialized) != 0) {
			m_script_runtime->call(TickFunctionList::init);
		}
		if ((reached & lifecycle_begun) != 0) {
			m_script_runtime->call(TickFunctionList::begin);
		}
		if ((reached & lifecycle_enabled) != 0 && enabled()) {
			m_script_runtime->call(TickFunctionList::on_enable);
		}
	}

	TOAST_INFO("Lua", "Reloaded scripts on {} ({}): {} value(s) preserved", name(), uid(), preserved);
	onScriptsReloaded();
}

void Node::callTick(const NodeInfo* info, TickFunctionList func_type) noexcept {
	if (!info) {
		TOAST_WARN("Node", "Tried to call a tick function but reflection data is null");
		return;
	}

	ZoneScoped;
	ZoneNameF("%s [%s] callTick(%s)", name().data(), info->type.data(), tickFunctionName(func_type).data());

	callTickNative(info, func_type);

	// After the c++ chain fire Lua scripts at the most-derived level
	if (info == m_info) {
		callTickScripts(func_type);
	}
}

void Node::callTickScripts(TickFunctionList func_type) noexcept {
	if (hasFlag(TickFunctionList::tick_mask, func_type) && not enabled()) {
		return;
	}
	if (m_script_runtime) {
		m_script_runtime->call(func_type);
	}
}

void Node::callTickNative(const NodeInfo* info, TickFunctionList func_type) noexcept {
	if (!info) {
		TOAST_WARN("Node", "Tried to call a tick function but reflection data is null");
		return;
	}

	ZoneScoped;
	ZoneNameF("%s [%s] callTick(%s)", name().data(), info->type.data(), tickFunctionName(func_type).data());

	// Frame-tick functions only run on enabled nodes; lifecycle/enable callbacks always run.
	if (hasFlag(TickFunctionList::tick_mask, func_type) && not enabled()) {
		return;
	}

	// Walk base → derived
	if (info->base_type) {
		callTickNative(info->base_type, func_type);
	}

	if (info == m_info) {
		switch (func_type) {
			case TickFunctionList::load: m_lifecycle |= lifecycle_loaded; break;
			case TickFunctionList::init: m_lifecycle |= lifecycle_initialized; break;
			case TickFunctionList::begin: m_lifecycle |= lifecycle_begun; break;
			case TickFunctionList::on_enable: m_lifecycle |= lifecycle_enabled; break;
			case TickFunctionList::on_disable: m_lifecycle &= static_cast<uint8_t>(~lifecycle_enabled); break;
			case TickFunctionList::end: m_lifecycle &= static_cast<uint8_t>(~(lifecycle_begun | lifecycle_enabled)); break;
			case TickFunctionList::destroy: m_lifecycle = 0; break;
			default: break;
		}
	}

	// Everything that assigns scripts builds the runtime itself
	if (info == m_info && func_type == TickFunctionList::init && !m_script_runtime && !m_scripts.empty()) {
		TOAST_WARN("Lua", "{} ({}) has scripts but no script runtime, so they will not run", name(), uid());
	}

	// Call this level's function
	const TickFunctions& funcs = info->functions;
	TickFunctions::Invoker invoker = nullptr;

	if (hasFlag(func_type, TickFunctionList::load) && hasFlag(funcs.list, TickFunctionList::load)) {
		invoker = funcs.load;
	} else if (hasFlag(func_type, TickFunctionList::save) && hasFlag(funcs.list, TickFunctionList::save)) {
		invoker = funcs.save;
	} else if (hasFlag(func_type, TickFunctionList::editor_tick) && hasFlag(funcs.list, TickFunctionList::editor_tick)) {
		invoker = funcs.editor_tick;
	} else if (hasFlag(func_type, TickFunctionList::init) && hasFlag(funcs.list, TickFunctionList::init)) {
		invoker = funcs.init;
	} else if (hasFlag(func_type, TickFunctionList::destroy) && hasFlag(funcs.list, TickFunctionList::destroy)) {
		invoker = funcs.destroy;
	} else if (hasFlag(func_type, TickFunctionList::begin) && hasFlag(funcs.list, TickFunctionList::begin)) {
		invoker = funcs.begin;
	} else if (hasFlag(func_type, TickFunctionList::end) && hasFlag(funcs.list, TickFunctionList::end)) {
		invoker = funcs.end;
	} else if (hasFlag(func_type, TickFunctionList::on_enable) && hasFlag(funcs.list, TickFunctionList::on_enable)) {
		invoker = funcs.on_enable;
	} else if (hasFlag(func_type, TickFunctionList::on_disable) && hasFlag(funcs.list, TickFunctionList::on_disable)) {
		invoker = funcs.on_disable;
	} else if (hasFlag(func_type, TickFunctionList::early_tick) && hasFlag(funcs.list, TickFunctionList::early_tick)) {
		invoker = funcs.early_tick;
	} else if (hasFlag(func_type, TickFunctionList::tick) && hasFlag(funcs.list, TickFunctionList::tick)) {
		invoker = funcs.tick;
	} else if (hasFlag(func_type, TickFunctionList::physics_tick) && hasFlag(funcs.list, TickFunctionList::physics_tick)) {
		invoker = funcs.physics_tick;
	} else if (hasFlag(func_type, TickFunctionList::post_physics) && hasFlag(funcs.list, TickFunctionList::post_physics)) {
		invoker = funcs.post_physics;
	} else if (hasFlag(func_type, TickFunctionList::late_tick) && hasFlag(funcs.list, TickFunctionList::late_tick)) {
		invoker = funcs.late_tick;
	}

	if (invoker) {
		ZoneScopedN("Function call");
		invoker(this);
	}

	switch (func_type) {
		case TickFunctionList::on_enable: on_enable.fire(this->box()); break;
		case TickFunctionList::on_disable: on_disable.fire(this->box()); break;
		case TickFunctionList::begin: on_begin.fire(this->box()); break;
		case TickFunctionList::end: on_end.fire(this->box()); break;
		default: break;
	}
}

void Node::propagateCallTick(const NodeInfo* info, TickFunctionList func_type) noexcept {
	ZoneScoped;
	ZoneNameF("propagateCallTick(%s) %s", tickFunctionName(func_type).data(), name().data());
	std::vector<Box<Node>> children = childrenSnapshot();

	callTick(info, func_type);

	for (Box<Node>& child : children) {
		if (child.exists()) {
			child->propagateCallTick(child->info(), func_type);
		}
	}
}

void Node::propagateEnable() noexcept {
	if (!enabled()) {
		return;
	}

	// Instantiation already set every inherited flag, so inheritedEnabled(true) would skip the children entirely.
	// Snapshot first: a child spawned inside onEnable runs its own propagateEnable and must not get a second call
	std::vector<Box<Node>> children = childrenSnapshot();
	callTick(info(), TickFunctionList::on_enable);
	for (auto& child : children) {
		if (child.exists()) {
			child->m_inherited_enabled = true;
			child->propagateEnable();
		}
	}
}

}
