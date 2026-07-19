#include "audio_ambience_volume.hpp"

#include "../audio_system.hpp"

#include <algorithm>
#include <toast/time.hpp>

namespace toast {

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
		return worldPos();
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
