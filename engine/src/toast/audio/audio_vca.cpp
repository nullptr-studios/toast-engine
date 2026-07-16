#include "audio_vca.hpp"

#include "audio_system.hpp"

namespace assets {

void AudioVca::setVolume(float volume) const {
	audio::AudioSystem::get().setVcaVolume(guid(), volume);
}

}
