#include "workspace_history.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <toast/events/event.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>
#include <unordered_set>

namespace toast {

struct WorkspaceHistory::PendingMerge {
	struct Action {
		enum class Kind : uint8_t {
			name,
			type,
			field,
			lua_value,
			existence,
			order
		};
		Kind kind = Kind::field;
		UID node;
		std::string field;
		std::optional<assets::Prefab::Field> base_field;
		std::optional<assets::Prefab::Field> current_field;
		std::optional<assets::Prefab::Field> incoming_field;
		std::optional<assets::Prefab::BasicNode> base_node;
		std::optional<assets::Prefab::BasicNode> current_node;
		std::optional<assets::Prefab::BasicNode> incoming_node;
		std::optional<std::string> base_text;
		std::optional<std::string> current_text;
		std::optional<std::string> incoming_text;
		std::vector<uint64_t> base_order;
		std::vector<uint64_t> current_order;
		std::vector<uint64_t> incoming_order;
	};

	uint64_t request = 0;
	bool is_merge = false;
	std::shared_ptr<const Snapshot> base;
	std::shared_ptr<const Snapshot> current;
	std::shared_ptr<const Snapshot> incoming;
	std::shared_ptr<Snapshot> result;
	std::vector<event::HistoryConflict> conflicts;
	std::vector<Action> actions;
};

WorkspaceHistory::~WorkspaceHistory() = default;

namespace {

auto uidOf(const assets::Prefab::BasicNode& node) -> UID {
	if (auto field = node.find("m_uid")) {
		if (auto* uid = std::any_cast<UID>(&field->value)) {
			return *uid;
		}
	}
	return {};
}

auto findName(const assets::Prefab& snapshot, UID uid) -> std::string {
	for (const auto& node : snapshot.nodes) {
		if (uidOf(node).data() == uid.data()) {
			return node.name;
		}
	}
	return {};
}

auto firstAddedUid(const assets::Prefab& before, const assets::Prefab& after) -> UID {
	std::unordered_set<uint64_t> old;
	for (const auto& node : before.nodes) {
		old.insert(uidOf(node).data());
	}
	for (const auto& node : after.nodes) {
		auto uid = uidOf(node);
		if (uid.data() != 0 && !old.contains(uid.data())) {
			return uid;
		}
	}
	return {};
}

auto findNode(const assets::Prefab& snapshot, UID uid) -> const assets::Prefab::BasicNode* {
	auto it = std::ranges::find_if(snapshot.nodes, [&](const auto& node) { return uidOf(node).data() == uid.data(); });
	return it == snapshot.nodes.end() ? nullptr : &*it;
}

auto findNode(assets::Prefab& snapshot, UID uid) -> assets::Prefab::BasicNode* {
	auto it = std::ranges::find_if(snapshot.nodes, [&](const auto& node) { return uidOf(node).data() == uid.data(); });
	return it == snapshot.nodes.end() ? nullptr : &*it;
}

auto mutableField(assets::Prefab::BasicNode& node, std::string_view name) -> assets::Prefab::Field* {
	if (auto it = std::ranges::find(node.fields, name, &assets::Prefab::Field::name); it != node.fields.end()) {
		return &*it;
	}
	for (auto& group : node.groups) {
		if (auto it = std::ranges::find(group.fields, name, &assets::Prefab::Field::name); it != group.fields.end()) {
			return &*it;
		}
		for (auto& subgroup : group.subgroups) {
			if (auto it = std::ranges::find(subgroup.fields, name, &assets::Prefab::Field::name); it != subgroup.fields.end()) {
				return &*it;
			}
		}
	}
	return nullptr;
}

void eraseField(assets::Prefab::BasicNode& node, std::string_view name) {
	auto erase = [name](auto& fields) { std::erase_if(fields, [name](const auto& field) { return field.name == name; }); };
	erase(node.fields);
	for (auto& group : node.groups) {
		erase(group.fields);
		for (auto& subgroup : group.subgroups) {
			erase(subgroup.fields);
		}
	}
}

void setField(assets::Prefab::BasicNode& node, std::string_view name, const std::optional<assets::Prefab::Field>& value) {
	if (!value) {
		eraseField(node, name);
		return;
	}
	if (auto* existing = mutableField(node, name)) {
		*existing = *value;
	} else {
		node.fields.push_back(*value);
	}
}

auto fieldsOf(const assets::Prefab::BasicNode& node) -> std::unordered_map<std::string, assets::Prefab::Field> {
	std::unordered_map<std::string, assets::Prefab::Field> result;
	for (const auto& field : node.fields) {
		result[field.name] = field;
	}
	for (const auto& group : node.groups) {
		for (const auto& field : group.fields) {
			result[field.name] = field;
		}
		for (const auto& subgroup : group.subgroups) {
			for (const auto& field : subgroup.fields) {
				result[field.name] = field;
			}
		}
	}
	return result;
}

auto fieldText(const std::optional<assets::Prefab::Field>& field) -> std::string {
	return field ? assets::Prefab::stringifyValue(field->type, field->is_array, field->value) : "<not set>";
}

auto fieldEqual(const std::optional<assets::Prefab::Field>& a, const std::optional<assets::Prefab::Field>& b) -> bool {
	if (!a || !b) {
		return a.has_value() == b.has_value();
	}
	return a->type == b->type && a->is_array == b->is_array && fieldText(a) == fieldText(b);
}

auto nodeFingerprint(const assets::Prefab::BasicNode& node) -> std::string {
	std::vector<std::string> parts {node.name, node.type};
	for (const auto& [name, field] : fieldsOf(node)) {
		parts.push_back(name + "=" + assets::Prefab::stringifyValue(field.type, field.is_array, field.value));
	}
	for (const auto& value : node.lua_vars) {
		parts.push_back("lua:" + value.path + "=" + value.value);
	}
	std::ranges::sort(parts);
	std::string result;
	for (const auto& part : parts) {
		result += part;
		result.push_back('\x1f');
	}
	return result;
}

auto luaValue(const assets::Prefab::BasicNode& node, std::string_view path) -> std::optional<std::string> {
	if (const auto* value = node.findLuaVar(path)) {
		return value->value;
	}
	return std::nullopt;
}

void setLuaValue(assets::Prefab::BasicNode& node, std::string_view path, const std::optional<std::string>& value) {
	auto it = std::ranges::find(node.lua_vars, path, &assets::Prefab::LuaVarOverride::path);
	if (!value) {
		if (it != node.lua_vars.end()) {
			node.lua_vars.erase(it);
		}
	} else if (it != node.lua_vars.end()) {
		it->value = *value;
	} else {
		node.lua_vars.push_back({std::string(path), *value});
	}
}

auto uidOrder(const assets::Prefab& snapshot) -> std::vector<uint64_t> {
	std::vector<uint64_t> result;
	result.reserve(snapshot.nodes.size());
	for (const auto& node : snapshot.nodes) {
		result.push_back(uidOf(node).data());
	}
	return result;
}

auto parentUid(const assets::Prefab::BasicNode& node) -> uint64_t {
	if (auto parent = node.find("m_parent")) {
		if (auto* uid = std::any_cast<UID>(&parent->value)) {
			return uid->data();
		}
	}
	return 0;
}

void pruneOrphans(assets::Prefab& snapshot) {
	bool changed;
	do {
		std::unordered_set<uint64_t> present;
		for (const auto& node : snapshot.nodes) {
			present.insert(uidOf(node).data());
		}
		const auto before = snapshot.nodes.size();
		std::erase_if(snapshot.nodes, [&](const auto& node) {
			auto parent = parentUid(node);
			return parent != 0 && !present.contains(parent);
		});
		changed = snapshot.nodes.size() != before;
	} while (changed);
}

void applyOrder(assets::Prefab& snapshot, const std::vector<uint64_t>& order) {
	std::unordered_map<uint64_t, assets::Prefab::BasicNode> nodes;
	for (auto& node : snapshot.nodes) {
		nodes.emplace(uidOf(node).data(), std::move(node));
	}
	snapshot.nodes.clear();
	for (auto uid : order) {
		if (auto it = nodes.find(uid); it != nodes.end()) {
			snapshot.nodes.push_back(std::move(it->second));
			nodes.erase(it);
		}
	}
	for (auto& [_, node] : nodes) {
		snapshot.nodes.push_back(std::move(node));
	}
}

auto optionalText(const std::optional<std::string>& value) -> std::string {
	return value.value_or("<not set>");
}

}

WorkspaceHistory::WorkspaceHistory(uint64_t handle, Capture capture, Restore restore, bool available, bool initially_saved)
    : m_handle(handle),
      m_capture(std::move(capture)),
      m_restore(std::move(restore)),
      m_available(available),
      m_initially_saved(initially_saved) { }

void WorkspaceHistory::sendInitial() const {
	ZoneScoped;
	event::WorkspaceHistoryInitialSnapshot initial;
	initial.workspace_handle = m_handle;
	initial.available = m_available;
	initial.initially_saved = m_initially_saved;
	initial.unavailable_reason = m_available ? "" : "History is unavailable during play mode";
	if (m_available) {
		initial.snapshot = m_capture().toBinary();
	}
	event::send<event::WorkspaceHistoryInitialSnapshot>(initial);
}

auto WorkspaceHistory::same(const Snapshot& a, const Snapshot& b) const -> bool {
	return a.toBinary() == b.toBinary();
}

void WorkspaceHistory::begin(uint64_t transaction, Context context) {
	ZoneScoped;
	if (!m_available || m_restoring || m_transaction) {
		return;
	}
	m_transaction = Transaction {
	  .id = transaction,
	  .context = std::move(context),
	  .before = std::make_shared<const Snapshot>(m_capture()),
	};
}

auto WorkspaceHistory::beginAtomic(Context context) -> bool {
	ZoneScoped;
	if (!m_available || m_restoring) {
		return false;
	}
	if (m_transaction) {
		if (m_transaction->id != 0 &&
		    (m_transaction->context.node.data() != context.node.data() || m_transaction->context.subject != context.subject)) {
			commit(m_transaction->id);
		} else {
			if (m_transaction->context.node_name.empty()) {
				m_transaction->context.node_name = context.node_name;
			}
			if (!m_transaction->observed_mutation) {
				m_transaction->context.previous_value = context.previous_value;
				m_transaction->observed_mutation = true;
			}
			m_transaction->context.current_value = context.current_value;
			return false;
		}
	}
	begin(0, std::move(context));
	return m_transaction.has_value();
}

void WorkspaceHistory::finishAtomic(bool owned) {
	if (owned) {
		commit();
	}
}

void WorkspaceHistory::commit(uint64_t transaction) {
	ZoneScoped;
	if (!m_available || !m_transaction) {
		return;
	}
	if (transaction != 0 && m_transaction->id != transaction) {
		return;
	}

	auto finished = std::move(*m_transaction);
	m_transaction.reset();
	auto after = std::make_shared<const Snapshot>(m_capture());
	if (same(*finished.before, *after)) {
		return;
	}

	if (finished.context.node.data() == 0) {
		finished.context.node = firstAddedUid(*finished.before, *after);
	}
	if (finished.context.node_name.empty()) {
		finished.context.node_name = findName(*after, finished.context.node);
		if (finished.context.node_name.empty()) {
			finished.context.node_name = findName(*finished.before, finished.context.node);
		}
	}
	event::WorkspaceHistoryCommitted committed;
	committed.workspace_handle = m_handle;
	committed.before_snapshot = finished.before->toBinary();
	committed.after_snapshot = after->toBinary();
	committed.node_uid = finished.context.node;
	committed.node_name = finished.context.node_name;
	committed.operation = finished.context.operation;
	committed.subject = finished.context.subject;
	committed.previous_value = finished.context.previous_value;
	committed.current_value = finished.context.current_value;
	event::send<event::WorkspaceHistoryCommitted>(committed);
}

void WorkspaceHistory::apply(const event::WorkspaceApplyHistorySnapshot& request) {
	ZoneScoped;
	event::WorkspaceHistorySnapshotApplied result;
	result.workspace_handle = m_handle;
	result.request = request.request;
	if (!m_available || m_transaction || request.snapshot.empty()) {
		if (!m_available) {
			result.error = "History is unavailable";
		} else if (m_transaction) {
			result.error = "Finish the current inspector edit before navigating history";
		} else {
			result.error = "The history snapshot is empty";
		}
		event::send<event::WorkspaceHistorySnapshotApplied>(result);
		return;
	}

	try {
		Snapshot snapshot(std::span<const uint8_t>(request.snapshot));
		if (!snapshot.validate()) {
			result.error = "The history snapshot is invalid";
		} else {
			m_restoring = true;
			result.success = m_restore(snapshot);
			m_restoring = false;
			if (!result.success) {
				result.error = "The workspace rejected the history snapshot";
			}
		}
	} catch (const std::exception& error) {
		m_restoring = false;
		result.error = error.what();
	}
	event::send<event::WorkspaceHistorySnapshotApplied>(result);
}

void WorkspaceHistory::cancel(uint64_t transaction) {
	ZoneScoped;
	if (!m_available || !m_transaction) {
		return;
	}
	if (transaction != 0 && m_transaction->id != transaction) {
		return;
	}
	auto before = m_transaction->before;
	m_transaction.reset();
	m_restoring = true;
	m_restore(*before);
	m_restoring = false;
}

void WorkspaceHistory::prepareMerge(const event::WorkspacePrepareHistoryMerge& request) {
	ZoneScoped;
	if (!m_available || m_transaction || m_pending) {
		event::WorkspaceHistoryMergePrepared result;
		result.workspace_handle = m_handle;
		result.request = request.request;
		result.error = m_pending ? "Another history merge is already in progress" : "History is unavailable";
		event::send<event::WorkspaceHistoryMergePrepared>(result);
		return;
	}

	try {
		auto base = std::make_shared<const Snapshot>(std::span<const uint8_t>(request.base_snapshot));
		auto current = std::make_shared<const Snapshot>(std::span<const uint8_t>(request.current_snapshot));
		auto incoming = std::make_shared<const Snapshot>(std::span<const uint8_t>(request.incoming_snapshot));
		if (!base->validate() || !current->validate() || !incoming->validate()) {
			event::WorkspaceHistoryMergePrepared result;
			result.workspace_handle = m_handle;
			result.request = request.request;
			result.error = "One or more merge snapshots are invalid";
			event::send<event::WorkspaceHistoryMergePrepared>(result);
			return;
		}
		beginMerge(request.request, request.is_merge, std::move(base), std::move(current), std::move(incoming));
	} catch (const std::exception& error) {
		event::WorkspaceHistoryMergePrepared result;
		result.workspace_handle = m_handle;
		result.request = request.request;
		result.error = error.what();
		event::send<event::WorkspaceHistoryMergePrepared>(result);
	}
}

void WorkspaceHistory::beginMerge(
    uint64_t request, bool merge_operation, std::shared_ptr<const Snapshot> base, std::shared_ptr<const Snapshot> current,
    std::shared_ptr<const Snapshot> incoming
) {
	ZoneScoped;
	m_pending = std::make_unique<PendingMerge>();
	m_pending->request = request;
	m_pending->is_merge = merge_operation;
	m_pending->base = std::move(base);
	m_pending->current = std::move(current);
	m_pending->incoming = std::move(incoming);
	m_pending->result = std::make_shared<Snapshot>(*m_pending->current);

	using Action = PendingMerge::Action;
	auto add_conflict = [&](Action action,
	                        event::HistoryConflictKind kind,
	                        std::string field,
	                        std::string base,
	                        std::string current,
	                        std::string incoming) {
		event::HistoryConflict conflict;
		conflict.id = m_pending->conflicts.size() + 1;
		conflict.node_uid = action.node;
		conflict.node_name = findName(*m_pending->current, action.node);
		if (conflict.node_name.empty()) {
			conflict.node_name = findName(*m_pending->incoming, action.node);
		}
		conflict.kind = kind;
		conflict.field = std::move(field);
		conflict.base_value = std::move(base);
		conflict.current_value = std::move(current);
		conflict.incoming_value = std::move(incoming);
		if (action.incoming_field || action.current_field || action.base_field) {
			const auto* typed = action.incoming_field ? &*action.incoming_field : nullptr;
			if (!typed && action.current_field) {
				typed = &*action.current_field;
			}
			if (!typed) {
				typed = &*action.base_field;
			}
			conflict.value_type = static_cast<uint32_t>(typed->type);
			conflict.is_array = typed->is_array;
		}
		m_pending->actions.push_back(std::move(action));
		m_pending->conflicts.push_back(std::move(conflict));
	};

	auto erase_result_node = [&](UID uid) {
		std::erase_if(m_pending->result->nodes, [&](const auto& node) { return uidOf(node).data() == uid.data(); });
	};
	auto put_result_node = [&](const assets::Prefab::BasicNode& node) {
		auto uid = uidOf(node);
		if (auto* existing = findNode(*m_pending->result, uid)) {
			*existing = node;
		} else {
			m_pending->result->nodes.push_back(node);
		}
	};

	for (const auto& base_node : m_pending->base->nodes) {
		auto uid = uidOf(base_node);
		const auto* current_node = findNode(*m_pending->current, uid);
		const auto* incoming_node = findNode(*m_pending->incoming, uid);
		if (!incoming_node) {
			if (!current_node) {
				continue;
			}
			if (nodeFingerprint(*current_node) == nodeFingerprint(base_node)) {
				erase_result_node(uid);
			} else {
				Action action {.kind = Action::Kind::existence, .node = uid};
				action.base_node = base_node;
				action.current_node = *current_node;
				add_conflict(std::move(action), event::HistoryConflictKind::existence, "Existence", "Exists", "Modified", "Deleted");
			}
			continue;
		}
		if (!current_node) {
			if (nodeFingerprint(*incoming_node) != nodeFingerprint(base_node)) {
				Action action {.kind = Action::Kind::existence, .node = uid};
				action.base_node = base_node;
				action.incoming_node = *incoming_node;
				add_conflict(std::move(action), event::HistoryConflictKind::existence, "Existence", "Exists", "Deleted", "Modified");
			}
			continue;
		}

		auto merge_text = [&](Action::Kind action_kind,
		                      event::HistoryConflictKind conflict_kind,
		                      std::string field,
		                      const std::string& base,
		                      const std::string& current,
		                      const std::string& incoming,
		                      auto&& apply) {
			if (incoming == base || incoming == current) {
				return;
			}
			if (current == base) {
				apply(incoming);
				return;
			}
			Action action {.kind = action_kind, .node = uid, .field = field};
			action.base_text = base;
			action.current_text = current;
			action.incoming_text = incoming;
			add_conflict(std::move(action), conflict_kind, std::move(field), base, current, incoming);
		};

		auto* result_node = findNode(*m_pending->result, uid);
		merge_text(
		    Action::Kind::name,
		    event::HistoryConflictKind::name,
		    "Name",
		    base_node.name,
		    current_node->name,
		    incoming_node->name,
		    [&](const auto& value) { result_node->name = value; }
		);
		merge_text(
		    Action::Kind::type,
		    event::HistoryConflictKind::type,
		    "Type",
		    base_node.type,
		    current_node->type,
		    incoming_node->type,
		    [&](const auto& value) { result_node->type = value; }
		);

		auto base_fields = fieldsOf(base_node);
		auto current_fields = fieldsOf(*current_node);
		auto incoming_fields = fieldsOf(*incoming_node);
		std::unordered_set<std::string> field_names;
		for (const auto& [name, _] : base_fields) {
			field_names.insert(name);
		}
		for (const auto& [name, _] : current_fields) {
			field_names.insert(name);
		}
		for (const auto& [name, _] : incoming_fields) {
			field_names.insert(name);
		}
		field_names.erase("m_uid");
		for (const auto& name : field_names) {
			auto get = [&](const auto& fields) -> std::optional<assets::Prefab::Field> {
				if (auto it = fields.find(name); it != fields.end()) {
					return it->second;
				}
				return std::nullopt;
			};
			auto base_value = get(base_fields);
			auto current_value = get(current_fields);
			auto incoming_value = get(incoming_fields);
			if (fieldEqual(incoming_value, base_value) || fieldEqual(incoming_value, current_value)) {
				continue;
			}
			if (fieldEqual(current_value, base_value)) {
				setField(*result_node, name, incoming_value);
				continue;
			}
			Action action {.kind = Action::Kind::field, .node = uid, .field = name};
			action.base_field = base_value;
			action.current_field = current_value;
			action.incoming_field = incoming_value;
			auto kind = event::HistoryConflictKind::value;
			if (name == "m_parent") {
				kind = event::HistoryConflictKind::parent;
			} else if (name == "m_local_enabled") {
				kind = event::HistoryConflictKind::enabled;
			}
			add_conflict(std::move(action), kind, name, fieldText(base_value), fieldText(current_value), fieldText(incoming_value));
		}

		std::unordered_set<std::string> lua_paths;
		for (const auto& value : base_node.lua_vars) {
			lua_paths.insert(value.path);
		}
		for (const auto& value : current_node->lua_vars) {
			lua_paths.insert(value.path);
		}
		for (const auto& value : incoming_node->lua_vars) {
			lua_paths.insert(value.path);
		}
		for (const auto& path : lua_paths) {
			auto base_value = luaValue(base_node, path);
			auto current_value = luaValue(*current_node, path);
			auto incoming_value = luaValue(*incoming_node, path);
			if (incoming_value == base_value || incoming_value == current_value) {
				continue;
			}
			if (current_value == base_value) {
				setLuaValue(*result_node, path, incoming_value);
				continue;
			}
			Action action {.kind = Action::Kind::lua_value, .node = uid, .field = path};
			action.base_text = base_value;
			action.current_text = current_value;
			action.incoming_text = incoming_value;
			add_conflict(
			    std::move(action),
			    event::HistoryConflictKind::lua_value,
			    path,
			    optionalText(base_value),
			    optionalText(current_value),
			    optionalText(incoming_value)
			);
		}
	}

	for (const auto& incoming_node : m_pending->incoming->nodes) {
		auto uid = uidOf(incoming_node);
		if (findNode(*m_pending->base, uid)) {
			continue;
		}
		if (const auto* current_node = findNode(*m_pending->current, uid)) {
			if (nodeFingerprint(*current_node) != nodeFingerprint(incoming_node)) {
				Action action {.kind = Action::Kind::existence, .node = uid};
				action.current_node = *current_node;
				action.incoming_node = incoming_node;
				add_conflict(
				    std::move(action),
				    event::HistoryConflictKind::existence,
				    "Existence",
				    "Did not exist",
				    "Created differently",
				    "Created differently"
				);
			}
		} else {
			put_result_node(incoming_node);
		}
	}

	auto base_order = uidOrder(*m_pending->base);
	auto current_order = uidOrder(*m_pending->current);
	auto incoming_order = uidOrder(*m_pending->incoming);
	auto sorted_base = base_order;
	auto sorted_current = current_order;
	auto sorted_incoming = incoming_order;
	std::ranges::sort(sorted_base);
	std::ranges::sort(sorted_current);
	std::ranges::sort(sorted_incoming);
	if (sorted_base == sorted_current && sorted_base == sorted_incoming && incoming_order != base_order &&
	    incoming_order != current_order) {
		if (current_order == base_order) {
			applyOrder(*m_pending->result, incoming_order);
		} else {
			Action action {.kind = Action::Kind::order};
			action.base_order = base_order;
			action.current_order = current_order;
			action.incoming_order = incoming_order;
			add_conflict(
			    std::move(action), event::HistoryConflictKind::order, "Node order", "Original order", "Current order", "Incoming order"
			);
		}
	}

	if (!m_pending->conflicts.empty()) {
		event::WorkspaceHistoryConflicts conflicts;
		conflicts.workspace_handle = m_handle;
		conflicts.request = m_pending->request;
		conflicts.is_merge = merge_operation;
		conflicts.source_revision = 0;
		conflicts.conflicts = m_pending->conflicts;
		event::send<event::WorkspaceHistoryConflicts>(conflicts);
		return;
	}

	event::WorkspaceResolveHistoryConflicts accepted;
	accepted.workspace_handle = m_handle;
	accepted.request = m_pending->request;
	resolve(accepted);
}

void WorkspaceHistory::resolve(const event::WorkspaceResolveHistoryConflicts& resolutions) {
	ZoneScoped;
	if (!m_pending || resolutions.request != m_pending->request) {
		m_pending.reset();
		return;
	}
	if (resolutions.cancel) {
		event::WorkspaceHistoryMergePrepared result;
		result.workspace_handle = m_handle;
		result.request = m_pending->request;
		result.error = "Merge cancelled";
		event::send<event::WorkspaceHistoryMergePrepared>(result);
		m_pending.reset();
		return;
	}

	for (size_t i = 0; i < m_pending->actions.size(); ++i) {
		const auto& action = m_pending->actions[i];
		const auto conflict_id = m_pending->conflicts[i].id;
		auto resolution = std::ranges::find(resolutions.resolutions, conflict_id, &event::HistoryConflictResolution::conflict);
		auto choice =
		    resolution == resolutions.resolutions.end() ? event::HistoryConflictResolution::Choice::current : resolution->choice;
		if (choice == event::HistoryConflictResolution::Choice::current) {
			continue;
		}

		auto select_text = [&]() -> std::optional<std::string> {
			if (choice == event::HistoryConflictResolution::Choice::base) {
				return action.base_text;
			}
			if (choice == event::HistoryConflictResolution::Choice::incoming) {
				return action.incoming_text;
			}
			return resolution->custom_value;
		};
		auto select_field = [&]() -> std::optional<assets::Prefab::Field> {
			if (choice == event::HistoryConflictResolution::Choice::base) {
				return action.base_field;
			}
			if (choice == event::HistoryConflictResolution::Choice::incoming) {
				return action.incoming_field;
			}
			const auto* shape = action.incoming_field ? &*action.incoming_field : nullptr;
			if (!shape && action.current_field) {
				shape = &*action.current_field;
			}
			if (!shape && action.base_field) {
				shape = &*action.base_field;
			}
			if (!shape) {
				return std::nullopt;
			}
			auto parsed = assets::Prefab::valueFromString(shape->type, shape->is_array, resolution->custom_value);
			if (!parsed) {
				return action.current_field;
			}
			auto custom = *shape;
			custom.value = std::move(*parsed);
			return custom;
		};

		switch (action.kind) {
			case PendingMerge::Action::Kind::name:
				if (auto* node = findNode(*m_pending->result, action.node); node && select_text()) {
					node->name = *select_text();
				}
				break;
			case PendingMerge::Action::Kind::type:
				if (auto* node = findNode(*m_pending->result, action.node); node && select_text()) {
					node->type = *select_text();
				}
				break;
			case PendingMerge::Action::Kind::field:
				if (auto* node = findNode(*m_pending->result, action.node)) {
					setField(*node, action.field, select_field());
				}
				break;
			case PendingMerge::Action::Kind::lua_value:
				if (auto* node = findNode(*m_pending->result, action.node)) {
					setLuaValue(*node, action.field, select_text());
				}
				break;
			case PendingMerge::Action::Kind::existence: {
				const auto& selected = choice == event::HistoryConflictResolution::Choice::base ? action.base_node : action.incoming_node;
				std::erase_if(m_pending->result->nodes, [&](const auto& node) { return uidOf(node).data() == action.node.data(); });
				if (selected) {
					m_pending->result->nodes.push_back(*selected);
				}
				break;
			}
			case PendingMerge::Action::Kind::order:
				applyOrder(
				    *m_pending->result,
				    choice == event::HistoryConflictResolution::Choice::base ? action.base_order : action.incoming_order
				);
				break;
		}
	}
	pruneOrphans(*m_pending->result);

	if (!m_pending->is_merge && same(*m_pending->result, *m_pending->current)) {
		event::WorkspaceHistoryMergePrepared result;
		result.workspace_handle = m_handle;
		result.request = m_pending->request;
		result.success = true;
		result.snapshot = m_pending->current->toBinary();
		event::send<event::WorkspaceHistoryMergePrepared>(result);
		m_pending.reset();
		return;
	}

	auto pending = std::move(m_pending);
	event::WorkspaceHistoryMergePrepared result;
	result.workspace_handle = m_handle;
	result.request = pending->request;
	result.success = true;
	result.snapshot = pending->result->toBinary();
	event::send<event::WorkspaceHistoryMergePrepared>(result);
}

}
