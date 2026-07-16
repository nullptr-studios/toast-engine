#include "music_player.hpp"

#include "../audio_events.hpp"
#include "../audio_system.hpp"

#include <algorithm>
#include <bit>
#include <fmod/fmod_studio.h>
#include <toast/log.hpp>
#include <toast/time.hpp>
#include <utility>

namespace toast {

namespace {

static_assert(
    sizeof(MusicPlayer::ParamID) == sizeof(FMOD_STUDIO_PARAMETER_ID),
    "ParamID must be ABI-compatible with FMOD_STUDIO_PARAMETER_ID"
);

auto F_CALL musicPlayerCallback(FMOD_STUDIO_EVENT_CALLBACK_TYPE type, FMOD_STUDIO_EVENTINSTANCE* inst, void* parameters)
    -> FMOD_RESULT {
	MusicPlayer::CallbackData* data = nullptr;
	FMOD_Studio_EventInstance_GetUserData(inst, reinterpret_cast<void**>(&data));
	if (!data || !data->player) {
		return FMOD_OK;
	}

	MusicPlayer* self = data->player;
	uint64_t instance_id = data->instance_id;
	MusicPlayer::QueuedCb cb;
	cb.instance_id = instance_id;

	if (type == FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_BEAT) {
		auto* p = static_cast<FMOD_STUDIO_TIMELINE_BEAT_PROPERTIES*>(parameters);
		cb.type = MusicPlayer::CbType::beat;
		cb.bar = p->bar;
		cb.beat = p->beat;
		cb.position_ms = p->position;
		cb.tempo = p->tempo;
		cb.sig_upper = p->timesignatureupper;
		cb.sig_lower = p->timesignaturelower;
		self->queueCallback(cb);
	} else if (type == FMOD_STUDIO_EVENT_CALLBACK_NESTED_TIMELINE_BEAT) {
		auto* p = static_cast<FMOD_STUDIO_TIMELINE_NESTED_BEAT_PROPERTIES*>(parameters);
		cb.type = MusicPlayer::CbType::beat;
		cb.bar = p->properties.bar;
		cb.beat = p->properties.beat;
		cb.position_ms = p->properties.position;
		cb.tempo = p->properties.tempo;
		cb.sig_upper = p->properties.timesignatureupper;
		cb.sig_lower = p->properties.timesignaturelower;
		self->queueCallback(cb);
	} else if (type == FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_MARKER) {
		auto* p = static_cast<FMOD_STUDIO_TIMELINE_MARKER_PROPERTIES*>(parameters);
		cb.type = MusicPlayer::CbType::marker;
		cb.marker_name = p->name;
		cb.position_ms = p->position;
		self->queueCallback(cb);
	} else if (type == FMOD_STUDIO_EVENT_CALLBACK_STOPPED) {
		cb.type = MusicPlayer::CbType::stopped;
		self->queueCallback(cb);
	}

	return FMOD_OK;
}

}

void MusicPlayer::startTrack(int track_index, float fade_in) {
	if (track_index < 0 || static_cast<size_t>(track_index) >= m_tracks.size()) {
		TOAST_WARN("MusicPlayer", "Track index {} out of range", track_index);
		return;
	}

	auto& event = m_tracks[track_index];
	if (!event.hasValue()) {
		TOAST_WARN("MusicPlayer", "Track {} has no event assigned", track_index);
		return;
	}

	auto& sys = audio::AudioSystem::get();
	uint64_t id = sys.playEvent3D(event->guid());
	if (id == 0) {
		TOAST_ERROR("MusicPlayer", "Failed to start track {}", track_index);
		return;
	}

	sys.setPitch(id, m_pitch);

	float initial_vol = (fade_in > 0.0f) ? 0.0f : m_volume;
	sys.setVolume(id, initial_vol);

	ActiveTrack at;
	at.instance_id = id;
	at.track_index = track_index;
	at.current_vol = initial_vol;
	at.target_vol = m_volume;
	at.fade_duration = fade_in;
	at.fade_elapsed = 0.0f;
	m_active_tracks.push_back(at);

	// Keep CallbackData stable
	m_callback_data.push_back({this, id});
	CallbackData* cb_data = &m_callback_data.back();

	FMOD_STUDIO_EVENTINSTANCE* raw = sys.getRawInstance(id);
	if (raw) {
		FMOD_Studio_EventInstance_SetUserData(raw, cb_data);
		FMOD_Studio_EventInstance_SetCallback(
		    raw,
		    musicPlayerCallback,
		    FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_BEAT | FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_MARKER |
		        FMOD_STUDIO_EVENT_CALLBACK_NESTED_TIMELINE_BEAT | FMOD_STUDIO_EVENT_CALLBACK_STOPPED
		);
	}
}

void MusicPlayer::stopTrack(ActiveTrack& at, bool allow_fadeout) {
	auto& sys = audio::AudioSystem::get();

	FMOD_STUDIO_EVENTINSTANCE* raw = sys.getRawInstance(at.instance_id);
	if (raw) {
		FMOD_Studio_EventInstance_SetUserData(raw, nullptr);
		FMOD_Studio_EventInstance_SetCallback(raw, nullptr, 0);
	}

	sys.stopEvent3D(at.instance_id, allow_fadeout);

	std::erase_if(m_callback_data, [&](const CallbackData& cd) { return cd.instance_id == at.instance_id; });
	m_param_ids.erase(at.instance_id);
	at.instance_id = 0;
}

void MusicPlayer::play(int track_index, float fade_in) {
	startTrack(track_index, fade_in);
}

void MusicPlayer::stop(bool allow_fadeout) {
	for (auto& at : m_active_tracks) {
		if (at.instance_id != 0) {
			stopTrack(at, allow_fadeout);
		}
	}
	m_active_tracks.clear();
}

void MusicPlayer::crossfadeTo(int track_index, float duration) {
	for (auto& at : m_active_tracks) {
		at.target_vol = 0.0f;
		at.fade_duration = duration;
		at.fade_elapsed = 0.0f;
	}
	startTrack(track_index, duration);
}

void MusicPlayer::pause(bool value) {
	auto& sys = audio::AudioSystem::get();
	for (auto& at : m_active_tracks) {
		sys.pauseEvent(at.instance_id, value);
	}
}

void MusicPlayer::keyOff(int track_index) {
	auto& sys = audio::AudioSystem::get();
	for (size_t i = 0; i < m_active_tracks.size(); ++i) {
		if (track_index == -1 || std::cmp_equal(i, track_index)) {
			sys.keyOffEvent(m_active_tracks[i].instance_id);
		}
	}
}

auto MusicPlayer::getTimelinePosition(int track_index) const -> int {
	if (track_index < 0 || static_cast<size_t>(track_index) >= m_active_tracks.size()) {
		return -1;
	}
	return audio::AudioSystem::get().getTimelinePosition(m_active_tracks[track_index].instance_id);
}

void MusicPlayer::setTimelinePosition(int ms, int track_index) {
	if (track_index < 0 || static_cast<size_t>(track_index) >= m_active_tracks.size()) {
		return;
	}
	audio::AudioSystem::get().setTimelinePosition(m_active_tracks[track_index].instance_id, ms);
}

void MusicPlayer::setParameter(std::string_view name, float value, int track_index) {
	auto& sys = audio::AudioSystem::get();
	for (size_t i = 0; i < m_active_tracks.size(); ++i) {
		if (track_index != -1 && std::cmp_not_equal(i, track_index)) {
			continue;
		}
		uint64_t id = m_active_tracks[i].instance_id;
		if (id == 0) {
			continue;
		}

		auto& id_map = m_param_ids[id];
		std::string key(name);
		if (auto it = id_map.find(key); it != id_map.end()) {
			FMOD_STUDIO_PARAMETER_ID fmod_id = std::bit_cast<FMOD_STUDIO_PARAMETER_ID>(it->second);
			sys.setParameterByID(id, fmod_id, value);
		} else {
			FMOD_STUDIO_EVENTINSTANCE* raw = sys.getRawInstance(id);
			if (!raw) {
				continue;
			}
			FMOD_STUDIO_EVENTDESCRIPTION* desc = nullptr;
			FMOD_Studio_EventInstance_GetDescription(raw, &desc);
			if (!desc) {
				continue;
			}
			FMOD_STUDIO_PARAMETER_DESCRIPTION pdesc {};
			if (FMOD_Studio_EventDescription_GetParameterDescriptionByName(desc, key.c_str(), &pdesc) == FMOD_OK) {
				id_map[key] = std::bit_cast<ParamID>(pdesc.id);
				sys.setParameterByID(id, pdesc.id, value);
			} else {
				TOAST_WARN("MusicPlayer", "Parameter '{}' not found on track {}", name, i);
			}
		}
	}
}

void MusicPlayer::setParameter(std::string_view name, bool value, int track_index) {
	setParameter(name, value ? 1.0f : 0.0f, track_index);
}

void MusicPlayer::masterVolume(float value) {
	m_volume = std::clamp(value, 0.0f, 1.0f);
	auto& sys = audio::AudioSystem::get();
	for (auto& at : m_active_tracks) {
		at.target_vol = m_volume;
		if (at.fade_duration <= 0.0f) {
			at.current_vol = m_volume;
			sys.setVolume(at.instance_id, m_volume);
		}
	}
}

void MusicPlayer::masterPitch(float value) {
	m_pitch = std::clamp(value, 0.5f, 2.0f);
	auto& sys = audio::AudioSystem::get();
	for (auto& at : m_active_tracks) {
		sys.setPitch(at.instance_id, m_pitch);
	}
}

void MusicPlayer::onEnable() {
	if (m_play_on_enable && !m_tracks.empty()) {
		play(0);
	}
}

void MusicPlayer::onDisable() {
	stop(true);
}

void MusicPlayer::destroy() {
	stop(false);
}

void MusicPlayer::tick() {
	// Drain FMOD callback queue under lock, process on main thread
	std::vector<QueuedCb> local_cbs;
	{
		std::lock_guard lock(m_cb_mutex);
		local_cbs.swap(m_pending_cbs);
	}

	for (auto& cb : local_cbs) {
		if (cb.type == CbType::beat) {
			event::send<event::MusicBeat>(cb.bar, cb.beat, cb.position_ms, cb.tempo, cb.sig_upper, cb.sig_lower);
		} else if (cb.type == CbType::marker) {
			event::send<event::MusicMarker>(cb.marker_name, cb.position_ms);
		} else if (cb.type == CbType::stopped) {
			for (auto& at : m_active_tracks) {
				if (at.instance_id == cb.instance_id) {
					std::erase_if(m_callback_data, [&](const CallbackData& cd) { return cd.instance_id == at.instance_id; });
					m_param_ids.erase(at.instance_id);
					at.instance_id = 0;
					break;
				}
			}
			event::send<event::MusicStopped>();
		}
	}

	// Advance crossfade volumes
	float dt = static_cast<float>(Time::delta());
	auto& sys = audio::AudioSystem::get();

	for (auto& at : m_active_tracks) {
		if (at.instance_id == 0 || at.fade_duration <= 0.0f) {
			continue;
		}
		at.fade_elapsed += dt;
		float t = std::clamp(at.fade_elapsed / at.fade_duration, 0.0f, 1.0f);
		at.current_vol = at.current_vol + ((at.target_vol - at.current_vol) * t);
		if (t >= 1.0f) {
			at.current_vol = at.target_vol;
			at.fade_duration = 0.0f;
		}
		sys.setVolume(at.instance_id, at.current_vol);
	}

	// Remove tracks that have fully faded to zero
	std::erase_if(m_active_tracks, [&](ActiveTrack& at) {
		if (at.instance_id == 0) {
			return true;
		}
		if (at.target_vol <= 0.0f && at.current_vol <= 0.001f && at.fade_duration <= 0.0f) {
			stopTrack(at, false);
			return true;
		}
		return false;
	});
}

void MusicPlayer::queueCallback(const QueuedCb& cb) {
	std::lock_guard lock(m_cb_mutex);
	m_pending_cbs.push_back(cb);
}

}
