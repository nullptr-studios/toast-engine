#pragma once

#include "../assets.hpp"
#include "audio_volume.hpp"

#include <random>

namespace toast {

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

	float m_spawn_timer = 0.0f;
	std::mt19937 m_rng {std::random_device {}()};
};

}
