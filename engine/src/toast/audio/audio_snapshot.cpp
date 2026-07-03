#include "audio_snapshot.hpp"

#include "audio_system.hpp"

namespace assets {

void AudioSnapshot::setIntensity(float intensity) const {
	audio::AudioSystem::get().setSnapshotIntensity(guid(), intensity);
}

void AudioSnapshot::setEnabled(bool enabled) const {
	audio::AudioSystem::get().setSnapshotEnabled(guid(), enabled);
}

}
