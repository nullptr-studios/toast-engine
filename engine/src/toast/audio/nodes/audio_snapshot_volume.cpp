#include "audio_snapshot_volume.hpp"

#include "../audio_system.hpp"

#include <algorithm>
#include <toast/time.hpp>

namespace toast {

auto SnapshotVolume::evaluateTarget(const VolumeTarget& target, float weight) -> bool {
	float w = calculateWeight(target) * weight;
	if (w > 0.0f) {
		m_accumulated_weight = std::max(m_accumulated_weight, w);
		return trackTarget(target, true);
	}
	return trackTarget(target, false);
}

void SnapshotVolume::resetAccumulators() {
	AudioVolume::resetAccumulators();
	m_accumulated_weight = 0.0f;
}

void SnapshotVolume::onAudioTargetEnter(const VolumeTarget&) {
}

void SnapshotVolume::onAudioTargetExit(const VolumeTarget&) {
}

void SnapshotVolume::onEnable() {
	if (!m_snapshot.hasValue() || m_current_intensity <= 0.0f) {
		return;
	}

	auto& sys = audio::AudioSystem::get();
	sys.setSnapshotEnabled(std::string(m_snapshot->guid()), true);
	sys.setSnapshotIntensity(std::string(m_snapshot->guid()), m_current_intensity);
}

void SnapshotVolume::onDisable() {
	if (!m_snapshot.hasValue()) {
		return;
	}

	audio::AudioSystem::get().setSnapshotEnabled(std::string(m_snapshot->guid()), false);
}

void SnapshotVolume::onVolumeTick() {
	if (!m_snapshot.hasValue()) {
		return;
	}

	auto& sys = audio::AudioSystem::get();
	std::string guid {m_snapshot->guid()};

	float target = std::clamp(m_accumulated_weight, 0.0f, 1.0f);
	float dt = static_cast<float>(Time::delta());

	if (target > m_current_intensity) {
		if (m_fade_in > 0.001f) {
			m_current_intensity += dt / m_fade_in;
			m_current_intensity = std::min(m_current_intensity, target);
		} else {
			m_current_intensity = target;
		}
	} else if (target < m_current_intensity) {
		if (m_fade_out > 0.001f) {
			m_current_intensity -= dt / m_fade_out;
			m_current_intensity = std::max(m_current_intensity, target);
		} else {
			m_current_intensity = target;
		}
	}

	bool enabled = m_current_intensity > 0.001f;
	sys.setSnapshotEnabled(guid, enabled);
	sys.setSnapshotIntensity(guid, m_current_intensity);
}

}
