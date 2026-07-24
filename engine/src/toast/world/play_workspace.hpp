/**
 * @file play_workspace.hpp
 * @author Xein
 * @date 06 Jul 2026
 *
 * @brief Ticking clone of a Workspace used by the editor's play mode
 */

#pragma once

#include "tick_scheduler.hpp"
#include "workspace.hpp"

namespace toast {
/**
 * @brief A Workspace that actually runs game logic
 *
 * Created when the editor presses play, reruns the tick lifecycle (all nodes created from
 * scratch again)
 *
 * @see Workspace, TickScheduler, World
 */
class PlayWorkspace : public Workspace {
public:
	PlayWorkspace(UID handle, assets::Prefab& prefab);

	~PlayWorkspace() override;

	auto name() -> std::string override;

	void registerDependency(Node& from, Node& to) override;
	void unregisterDependency(Node& from, Node& to) override;

	void tick() override;

	[[nodiscard]]
	auto participatesIn(NodeOwnerParticipation use) const noexcept -> bool override;

private:
	TickScheduler m_scheduler;
	bool m_paused = false;
	bool m_schedule_dirty = true;
	void computeSchedule();
};
}
