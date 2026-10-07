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

#include <atomic>

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
	void registerInteraction(Node& first, Node& second) override;

	void tick() override;

	[[nodiscard]]
	auto participatesIn(NodeOwnerParticipation use) const noexcept -> bool override;

	[[nodiscard]]
	auto receivesEvents() const noexcept -> bool override {
		// The play workspace is the one running so it always receives events
		return true;
	}

	[[nodiscard]]
	static auto exists() noexcept -> bool {
			// True while any PlayWorkspace exists
		return s_instances.load(std::memory_order_acquire) > 0;
	}

protected:
	///@brief Playmode state
	[[nodiscard]]
	auto isPlaying() const -> bool override {
		return true;
	}

private:
	static inline std::atomic<int> s_instances = 0;

	TickScheduler m_scheduler;
	bool m_paused = false;
	bool m_started = false;
	std::atomic<bool> m_schedule_dirty = true;    // scripts register dependencies from init(), possibly on a loader thread
	void computeSchedule();
};
}
