#include "audio_volume.hpp"

#include "../audio_system.hpp"
#include "audio_listener.hpp"

#include <algorithm>

namespace toast {

void AudioVolume::begin() {
	audio::AudioSystem::get().registerVolume(*this);
}

void AudioVolume::end() {
	audio::AudioSystem::get().unregisterVolume(*this);
}

void AudioVolume::onEnable() { }

void AudioVolume::onDisable() { }

void AudioVolume::setListeners(const std::vector<Box<AudioListener>>& listeners) {
	m_listeners = listeners;
}

void AudioVolume::resetAccumulators() {
	m_pending_targets.clear();
}

auto AudioVolume::hasListenersInside() const -> bool {
	return not m_listeners_inside.empty();
}

auto AudioVolume::trackTarget(const VolumeTarget& target, bool inside) -> bool {
	auto pending = std::ranges::find_if(m_pending_targets, [&target](const ListenerState& state) {
		return state.target.listener == target.listener;
	});
	if (pending == m_pending_targets.end()) {
		m_pending_targets.push_back({target, inside});
	} else {
		pending->target = target;
		pending->inside = inside;
	}

	auto active = std::ranges::find_if(m_listeners_inside, [&target](const ListenerState& state) {
		return state.target.listener == target.listener;
	});
	if (inside) {
		if (active == m_listeners_inside.end()) {
			m_listeners_inside.push_back({target, true});
			onAudioTargetEnter(target);
		} else {
			active->target = target;
			active->inside = true;
		}
	}

	return inside;
}

void AudioVolume::finalizeAccumulators() {
	for (auto it = m_listeners_inside.begin(); it != m_listeners_inside.end();) {
		auto pending = std::ranges::find_if(m_pending_targets, [&it](const ListenerState& state) {
			return state.target.listener == it->target.listener;
		});

		if (pending == m_pending_targets.end() || not pending->inside) {
			ListenerState exiting = pending != m_pending_targets.end() ? *pending : *it;
			it = m_listeners_inside.erase(it);
			onAudioTargetExit(exiting.target);
		} else {
			it->target = pending->target;
			++it;
		}
	}
}

void AudioVolume::lateTick() {
	resetAccumulators();

	for (auto& listener : m_listeners) {
		if (!listener.exists()) {
			continue;
		}

		VolumeTarget target {
		  .listener = listener,
		  .position = listener->world_position,
		  .forward = listener->forward(),
		};
		evaluateTarget(target);
	}

	finalizeAccumulators();
	onVolumeTick();
}

}
