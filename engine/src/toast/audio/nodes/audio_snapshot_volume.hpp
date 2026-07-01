#pragma once

#include "../assets.hpp"
#include "audio_volume.hpp"

namespace toast {

class TOAST_API [[ToastNode, Color("Beige"), Icon("Area")]] SnapshotVolume : public AudioVolume {
public:
	auto evaluateTarget(const VolumeTarget& target, float weight = 1.0f) -> bool override;

	void resetAccumulators() override;

	void onAudioTargetEnter(const VolumeTarget& target) override;
	void onAudioTargetExit(const VolumeTarget& target) override;

private:
	void onVolumeTick() override;
	void onEnable();
	void onDisable();

	[[Reflect]]
	assets::AssetHandle<assets::AudioSnapshot> m_snapshot;

	[[Reflect, Unit("s")]]
	float m_fade_in = 0.2f;

	[[Reflect, Unit("s")]]
	float m_fade_out = 0.2f;

	float m_accumulated_weight = 0.0f;
	float m_current_intensity = 0.0f;
};

}
