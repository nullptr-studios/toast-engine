#include "audio_bus.hpp"

#include "audio_system.hpp"

namespace assets {

void AudioBus::setVolume(float volume) const {
	audio::AudioSystem::get().setBusVolume(guid(), volume);
}

}
