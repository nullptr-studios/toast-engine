/**
 * @file audio_ambience_volume.hpp
 * @author Xein
 * @date 01 Jul 2026
 *
 * @brief Randomly spawns one-shot audio events at positions inside the volume
 */
#pragma once

#include "../assets.hpp"
#include "audio_volume.hpp"

#include <random>

namespace toast {

/**
 * @brief Ambient sound generator
 *
 * Only active when at least one listener is inside. Picks a random event from the list,
 * spawns it at a random position inside the volume, and re-schedules at a random interval.
 * Useful for weather, bug swarms, and nature ambience
 */
class TOAST_API [[ToastNode]] AmbienceVolume : public AudioVolume {
public:
	auto evaluateTarget(const VolumeTarget& target, float weight = 1.0f) -> bool override;

	void onAudioTargetEnter(const VolumeTarget& target) override;
	void onAudioTargetExit(const VolumeTarget& target) override;

private:
	void onVolumeTick() override;

	void spawnAmbience();
	void scheduleNextSpawn();
	auto randomEvent() -> assets::AssetHandle<assets::AudioEvent>;
	auto randomSpawnPosition() -> glm::vec3;

	[[Reflect, Name("Audio Events")]]
	std::vector<assets::AssetHandle<assets::AudioEvent>> m_events;

	[[Reflect, Range(0.25, 30.0), Unit("s")]]
	float m_min_interval = 2.0f;

	[[Reflect, Range(0.25, 60.0), Unit("s")]]
	float m_max_interval = 8.0f;

	[[Reflect, Range(0.0, 1.0)]]
	float m_volume = 1.0f;

	float m_spawn_timer = 0.0f;    ///< countdown until next spawn, randomized in [min_interval, max_interval]
	std::mt19937 m_rng {std::random_device {}()};
};

}
