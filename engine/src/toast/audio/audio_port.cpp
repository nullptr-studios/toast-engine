#include "audio_port.hpp"

#include "audio_system.hpp"

namespace assets {

void AudioPort::setVolume(float volume) const {
	audio::AudioSystem::get().setPortVolume(guid(), volume);
}

}
