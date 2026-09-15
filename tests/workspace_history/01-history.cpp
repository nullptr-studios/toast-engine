#include "test_registry.hpp"

#include <cassert>
#include <toast/events/event.hpp>
#include <toast/events/listener.hpp>
#include <toast/world/workspace_history.hpp>

using namespace toast;

namespace {

auto fixture() -> assets::Prefab {
	assets::Prefab prefab;
	assets::Prefab::BasicNode root {.name = "Root", .type = "toast::Node"};
	root.fields.push_back({"m_uid", FieldType::uid_t, false, UID(1)});
	root.fields.push_back({"a", FieldType::int_t, false, 0});
	root.fields.push_back({"b", FieldType::int_t, false, 0});
	prefab.nodes.push_back(std::move(root));
	return prefab;
}

auto value(const assets::Prefab& prefab, std::string_view field) -> int {
	return prefab.nodes.front().find(field)->as<int>();
}

void setValue(assets::Prefab& prefab, std::string_view field, int value) {
	for (auto& candidate : prefab.nodes.front().fields) {
		if (candidate.name == field) {
			candidate.value = value;
			return;
		}
	}
	assert(false);
}

}

TOAST_TEST_NAMED("workspace_history", "workspace_history/01-history", test_workspace_history_01_history) {
	assets::Prefab live = fixture();
	event::Listener listener;
	std::optional<event::WorkspaceHistoryInitialSnapshot> initial;
	std::optional<event::WorkspaceHistoryCommitted> committed;
	std::optional<event::WorkspaceHistorySnapshotApplied> applied;
	std::optional<event::WorkspaceHistoryMergePrepared> prepared;
	std::optional<event::WorkspaceHistoryConflicts> conflict;
	listener.subscribe<event::WorkspaceHistoryInitialSnapshot>([&](const auto& update) { initial = update; });
	listener.subscribe<event::WorkspaceHistoryCommitted>([&](const auto& update) { committed = update; });
	listener.subscribe<event::WorkspaceHistorySnapshotApplied>([&](const auto& update) { applied = update; });
	listener.subscribe<event::WorkspaceHistoryMergePrepared>([&](const auto& update) { prepared = update; });
	listener.subscribe<event::WorkspaceHistoryConflicts>([&](const auto& update) { conflict = update; });

	WorkspaceHistory history(
	  42, [&] { return live; }, [&](const assets::Prefab& snapshot) {
		  live = snapshot;
		  return true;
	  },
	  true, true);
	history.sendInitial();
	event::pollEvents();
	assert(initial && initial->workspace_handle == 42 && initial->initially_saved);
	auto initial_bytes = initial->snapshot;

	WorkspaceHistory::Context gesture;
	gesture.operation = event::HistoryOperation::change_value;
	gesture.node = UID(1);
	gesture.subject = "a changed";
	history.begin(77, gesture);
	for (int next : {1, 2, 3}) {
		WorkspaceHistory::Context streamed = gesture;
		streamed.previous_value = std::to_string(value(live, "a"));
		streamed.current_value = std::to_string(next);
		const bool owned = history.beginAtomic(std::move(streamed));
		assert(!owned);
		setValue(live, "a", next);
		history.finishAtomic(owned);
	}
	history.commit(77);
	event::pollEvents();
	assert(committed && committed->subject == "a changed");
	assert(value(assets::Prefab(std::span<const uint8_t>(committed->before_snapshot)), "a") == 0);
	assert(value(assets::Prefab(std::span<const uint8_t>(committed->after_snapshot)), "a") == 3);

	event::WorkspaceApplyHistorySnapshot apply;
	apply.workspace_handle = 42;
	apply.request = 10;
	apply.snapshot = initial_bytes;
	history.apply(apply);
	event::pollEvents();
	assert(applied && applied->success && applied->request == 10);
	assert(value(live, "a") == 0);

	auto base = fixture();
	auto current = base;
	setValue(current, "b", 20);
	auto incoming = base;
	setValue(incoming, "a", 10);
	event::WorkspacePrepareHistoryMerge merge;
	merge.workspace_handle = 42;
	merge.request = 20;
	merge.is_merge = true;
	merge.base_snapshot = base.toBinary();
	merge.current_snapshot = current.toBinary();
	merge.incoming_snapshot = incoming.toBinary();
	history.prepareMerge(merge);
	event::pollEvents();
	assert(prepared && prepared->success && prepared->request == 20);
	auto merged = assets::Prefab(std::span<const uint8_t>(prepared->snapshot));
	assert(value(merged, "a") == 10 && value(merged, "b") == 20);

	prepared.reset();
	conflict.reset();
	setValue(current, "a", 200);
	setValue(incoming, "a", 100);
	merge.request = 21;
	merge.current_snapshot = current.toBinary();
	merge.incoming_snapshot = incoming.toBinary();
	history.prepareMerge(merge);
	event::pollEvents();
	assert(conflict && conflict->conflicts.size() == 1);
	assert(conflict->conflicts.front().field == "a");
	event::WorkspaceResolveHistoryConflicts resolution;
	resolution.workspace_handle = 42;
	resolution.request = conflict->request;
	resolution.resolutions.push_back({
	  .conflict = conflict->conflicts.front().id,
	  .choice = event::HistoryConflictResolution::Choice::incoming,
	});
	history.resolve(resolution);
	event::pollEvents();
	assert(prepared && prepared->success && prepared->request == 21);
	assert(value(assets::Prefab(std::span<const uint8_t>(prepared->snapshot)), "a") == 100);
}
