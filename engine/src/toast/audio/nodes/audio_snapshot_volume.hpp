/**
 * @file audio_snapshot_volume.hpp
 * @author Xein
 * @date 01 Jul 2026
 *
 * @brief Triggers an FMOD snapshot while listeners are inside the volume
 */
#pragma once

#include "../assets.hpp"
#include "audio_volume.hpp"

namespace toast {

/**
 * @brief Fades an FMOD snapshot in and out based on accumulated listener weight
 *
 * As listeners enter the volume, the snapshot intensity smoothly rises. As they leave,
 * it falls back to zero. Multiple overlapping snapshots combine additively
 */
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

	float m_accumulated_weight = 0.0f;    ///< total listener weight inside this frame, reset each tick
	float m_current_intensity = 0.0f;     ///< smoothed intensity lerping toward accumulated weight
};

}
