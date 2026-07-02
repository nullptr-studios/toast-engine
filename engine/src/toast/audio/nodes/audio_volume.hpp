/**
 * @file AudioVolume.hpp
 * @author Xein
 * @date 01 Jul 2026
 *
 * @brief Interfaces for all Audio Volume types
 */

#pragma once
#include <toast/export.hpp>
#include <toast/world/box.hpp>
#include <toast/world/volume.hpp>
#include <vector>

namespace toast {
class AudioListener;

/**
 * @brief Base interface for audio-reactive volumes
 *
 * Each frame, listener weights accumulate via evaluateTarget. After all listeners are
 * processed, finalizeAccumulators is called once, then onVolumeTick fires so subclasses
 * can push state to FMOD based on the frame's accumulated data
 */
class TOAST_API [[ToastNode, Hidden, Interface, Color("Beige")]] AudioVolume : public Volume {
public:
	virtual void onAudioTargetEnter(const VolumeTarget& target) = 0;
	virtual void onAudioTargetExit(const VolumeTarget& target) = 0;
	void resetAccumulators() override;
	void finalizeAccumulators();

	void setListeners(const std::vector<Box<AudioListener>>& listeners);

protected:
	[[nodiscard]]
	auto trackTarget(const VolumeTarget& target, bool inside) -> bool;  ///< returns true if the inside state changed
	[[nodiscard]]
	auto hasListenersInside() const -> bool;

	virtual void onVolumeTick() {}  ///< called after accumulators are finalized, safe to push to FMOD here

private:
	void begin();
	void end();
	void onEnable();
	void onDisable();
	void lateTick();

	struct ListenerState {
		VolumeTarget target;
		bool inside = false;
	};

	std::vector<ListenerState> m_listeners_inside;
	std::vector<ListenerState> m_pending_targets;
	std::vector<Box<AudioListener>> m_listeners;
};

}
