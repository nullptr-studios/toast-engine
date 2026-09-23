#include "audio_ambience_volume.hpp"

#include "../audio_system.hpp"

#include <algorithm>
#include <cmath>
#include <toast/time.hpp>

namespace toast {

void AmbienceVolume::updateInspectorMessages() {
	Volume::updateInspectorMessages();
	static const NodeMessage events_message {
	  .severity = NodeMessage::warning,
	  .id = 17,
	  .text = "AmbienceVolume requires one AudioEvent",
	};
	static const NodeMessage interval_message {
	  .severity = NodeMessage::error,
	  .id = 18,
	  .text = "Max must be at least Min",
	};

	const bool has_event = std::ranges::any_of(m_events, [](const auto& event) { return event.hasValue(); });
	if (has_event) {
		removeInspectorMessage(events_message);
	} else {
		addInspectorMessage(events_message);
	}
	if (std::isfinite(m_min_interval) && std::isfinite(m_max_interval) && m_min_interval >= 0.0f &&
	    m_max_interval >= m_min_interval) {
		removeInspectorMessage(interval_message);
	} else {
		addInspectorMessage(interval_message);
	}
}

auto AmbienceVolume::evaluateTarget(const VolumeTarget& target, float weight) -> bool {
	return trackTarget(target, calculateWeight(target) * weight > 0.0f);
}

void AmbienceVolume::onAudioTargetEnter(const VolumeTarget&) {
	m_spawn_timer = 0.0f;
}

void AmbienceVolume::onAudioTargetExit(const VolumeTarget&) {
	if (!hasListenersInside()) {
		m_spawn_timer = 0.0f;
	}
}

void AmbienceVolume::onVolumeTick() {
	if (!hasListenersInside() || m_events.empty()) {
		m_spawn_timer = 0.0f;
		return;
	}

	m_spawn_timer -= static_cast<float>(Time::delta());
	if (m_spawn_timer > 0.0f) {
		return;
	}

	spawnAmbience();
	scheduleNextSpawn();
}

void AmbienceVolume::spawnAmbience() {
	auto event = randomEvent();
	if (!event.hasValue()) {
		return;
	}

	auto& sys = audio::AudioSystem::get();
	uint64_t instance = sys.playEvent3D(event->guid());
	if (instance == 0) {
		return;
	}

	sys.set3DAttributes(instance, randomSpawnPosition(), {0.0f, 0.0f, 0.0f}, world_forward, world_up);
	sys.setVolume(instance, m_volume);
}

void AmbienceVolume::scheduleNextSpawn() {
	float min_interval = std::max(0.0f, m_min_interval);
	float max_interval = std::max(min_interval, m_max_interval);
	std::uniform_real_distribution<float> dist(min_interval, max_interval);
	m_spawn_timer = dist(m_rng);
}

auto AmbienceVolume::randomEvent() -> assets::Handle<assets::AudioEvent> {
	std::vector<assets::Handle<assets::AudioEvent>> candidates;
	candidates.reserve(m_events.size());
	for (const auto& event : m_events) {
		if (event.hasValue()) {
			candidates.push_back(event);
		}
	}
	if (candidates.empty()) {
		return {};
	}

	std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
	return candidates[dist(m_rng)];
}

auto AmbienceVolume::randomSpawnPosition() -> glm::vec3 {
	if (isGlobal()) {
		return world_position;
	}

	std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
	glm::vec3 local {
	  dist(m_rng),
	  dist(m_rng),
	  dist(m_rng),
	};
	return {getWorldTransform() * glm::vec4(local, 1.0f)};
}

}
