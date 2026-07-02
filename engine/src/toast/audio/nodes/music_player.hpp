#pragma once
#include "../assets.hpp"

#include <mutex>
#include <string>
#include <toast/export.hpp>
#include <toast/world/node.hpp>
#include <unordered_map>
#include <vector>

namespace toast {

class TOAST_API [[ToastNode, Color("Beige"), Icon("AudioStreamMP3")]] MusicPlayer : public Node {
public:
	void play(int track_index = 0, float fade_in = 0.0f);
	void stop(bool allow_fadeout = true);
	void crossfadeTo(int track_index, float duration);
	void pause(bool value);
	void keyOff(int track_index = -1);

	[[nodiscard]]
	auto getTimelinePosition(int track_index = 0) const -> int;
	void setTimelinePosition(int ms, int track_index = 0);

	void setParameter(std::string_view name, float value, int track_index = -1);
	void setParameter(std::string_view name, bool value, int track_index = -1);

	void masterVolume(float value);
	void masterPitch(float value);

private:
	void onEnable();
	void onDisable();
	void tick();
	void destroy();

public:
	struct ActiveTrack {
		uint64_t instance_id = 0;
		int track_index = -1;
		float current_vol = 0.0f;
		float target_vol = 1.0f;
		float fade_duration = 0.0f;
		float fade_elapsed = 0.0f;
	};

	struct CallbackData {
		MusicPlayer* player = nullptr;
		uint64_t instance_id = 0;
	};

	enum class CbType : uint8_t {
		beat,
		marker,
		stopped
	};

	struct QueuedCb {
		CbType type = CbType::beat;
		uint64_t instance_id = 0;
		int bar = 0;
		int beat = 0;
		int position_ms = 0;
		float tempo = 0.0f;
		int sig_upper = 4;
		int sig_lower = 4;
		std::string marker_name;
	};

	struct ParamID {
		unsigned int data1 = 0;
		unsigned int data2 = 0;
	};

	void queueCallback(const QueuedCb& cb);

private:
	void startTrack(int track_index, float fade_in);
	void stopTrack(ActiveTrack& at, bool allow_fadeout);

	[[Reflect, Name("Tracks")]]
	std::vector<assets::AssetHandle<assets::AudioEvent>> m_tracks;

	[[Reflect]]
	bool m_play_on_enable = true;

	[[Reflect, Range(0.0, 1.0)]]
	float m_volume = 1.0f;

	[[Reflect, Range(0.5, 2.0)]]
	float m_pitch = 1.0f;

	std::vector<ActiveTrack> m_active_tracks;
	std::vector<CallbackData> m_callback_data;

	std::mutex m_cb_mutex;
	std::vector<QueuedCb> m_pending_cbs;

	std::unordered_map<uint64_t, std::unordered_map<std::string, ParamID>> m_param_ids;
};

}
