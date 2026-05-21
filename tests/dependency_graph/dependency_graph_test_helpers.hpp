#pragma once

#include "toast/world/world_test_access.hpp"

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace toast::tests::dependency_graph {

using ItemSnapshot = std::vector<std::string>;
using WaveSnapshot = std::vector<ItemSnapshot>;
using ScheduleSnapshot = std::vector<WaveSnapshot>;

enum class Stage {
	early_tick,
	tick,
	post_physics,
	late_tick,
};

inline auto item(std::string_view name) -> ItemSnapshot {
	return ItemSnapshot {std::string(name)};
}

inline auto cluster(std::initializer_list<std::string_view> names) -> ItemSnapshot {
	ItemSnapshot result;
	result.reserve(names.size());
	for (auto name : names) {
		result.emplace_back(name);
	}
	std::sort(result.begin(), result.end());
	return result;
}

inline auto wave(std::initializer_list<ItemSnapshot> items) -> WaveSnapshot {
	WaveSnapshot result(items.begin(), items.end());
	std::sort(result.begin(), result.end());
	return result;
}

inline auto schedule(std::initializer_list<WaveSnapshot> waves) -> ScheduleSnapshot {
	return ScheduleSnapshot(waves.begin(), waves.end());
}

template<typename StageSchedule>
auto snapshotSchedule(const StageSchedule& stage_schedule) -> ScheduleSnapshot {
	ScheduleSnapshot result;
	result.reserve(stage_schedule.size());

	for (const auto& wave : stage_schedule) {
		WaveSnapshot wave_snapshot;
		wave_snapshot.reserve(wave.size());

		for (const auto& item_variant : wave) {
			ItemSnapshot names;
			std::visit(
			    [&names](const auto& value) {
				    using T = std::decay_t<decltype(value)>;
				    if constexpr (std::is_same_v<T, toast::Box<toast::Node>>) {
					    names.emplace_back(std::string(value->name()));
				    } else {
					    names.reserve(value.nodes.size());
					    for (const auto& node : value.nodes) {
						    names.emplace_back(std::string(node->name()));
					    }
					    std::sort(names.begin(), names.end());
				    }
			    },
			    item_variant
			);
			wave_snapshot.emplace_back(std::move(names));
		}

		std::sort(wave_snapshot.begin(), wave_snapshot.end());
		result.emplace_back(std::move(wave_snapshot));
	}

	return result;
}

inline void assertScheduleEquals(const ScheduleSnapshot& actual, const ScheduleSnapshot& expected) {
	assert(actual == expected);
}

inline void addStageFunction(toast::Node& node, Stage stage) {
	auto& table = toast::_detail::WorldTestAccess::functionTable(node).table;
	switch (stage) {
		case Stage::early_tick:
			table.early_tick.emplace_back([](toast::Node*) {});
			break;
		case Stage::tick:
			table.tick.emplace_back([](toast::Node*) {});
			break;
		case Stage::post_physics:
			table.post_physics.emplace_back([](toast::Node*) {});
			break;
		case Stage::late_tick:
			table.late_tick.emplace_back([](toast::Node*) {});
			break;
	}
}

inline auto scheduleFor(toast::World& world, Stage stage) -> ScheduleSnapshot {
	const auto& tick_schedule = toast::_detail::WorldTestAccess::tickSchedule(world);
	switch (stage) {
		case Stage::early_tick:
			return snapshotSchedule(tick_schedule.early_tick);
		case Stage::tick:
			return snapshotSchedule(tick_schedule.tick);
		case Stage::post_physics:
			return snapshotSchedule(tick_schedule.post_physics);
		case Stage::late_tick:
			return snapshotSchedule(tick_schedule.late_tick);
	}
	return {};
}

}
