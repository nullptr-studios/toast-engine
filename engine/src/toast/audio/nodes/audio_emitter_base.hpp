#pragma once
#include "../assets.hpp"

#include <toast/export.hpp>
#include <toast/world/node_3d.hpp>

namespace toast {

class TOAST_API [[ToastNode, Hidden, Color("Beige"), Icon("AudioStreamPlayer")]] AudioEmitterBase : public Node3D {
public:
	[[Button]]
	void play();

	[[Button]]
	void stop();

	void pause(bool value) const;
	void setParameter(std::string_view name, float value) const;
	void setParameter(std::string_view name, bool value) const;
	[[nodiscard]] auto isPlaying() const -> bool;

	void event(std::string_view path);
	void event(toast::UID uid);
	void playOnEnable(bool value);
	void volume(float value);
	void pitch(float value);
	void allowFadeout(bool value);

	void dopplerScale(float value);
	void calculateVelocity(bool value);
	void overrideAttenuation(bool value);
	void minDistance(float value);
	void maxDistance(float value);

protected:
	virtual auto emitterPosition(const glm::vec3& listener) -> glm::vec3;

	virtual auto emitterForward() -> glm::vec3;
	virtual auto emitterUp() -> glm::vec3;

private:
	void onEnable();
	void onDisable();
	void begin();
	void lateTick();
	void update3DState();
	void applyProperties() const;

	[[Reflect, Name("Audio Event")]]
	assets::AssetHandle<assets::AudioEvent> m_event;

	[[Reflect]]
	bool m_play_on_enable = false;

	[[Reflect, Range(0.0, 1.0)]]
	float m_volume = 1.0f;

	[[Reflect, Range(0.5, 2.0)]]
	float m_pitch = 1.0f;

	[[Reflect]]
	bool m_allow_fadeout = true;

	[[Reflect, Range(0.0, 5.0)]]
	float m_doppler_scale = 1.0f;

	[[Reflect]]
	bool m_calculate_velocity = true;

	[[Reflect, Group("Override Attenuation")]]
	bool m_override_attenuation = false;

	[[Reflect, Group("Override Attenuation")]]
	float m_min_distance = 1.0f;

	[[Reflect, Group("Override Attenuation")]]
	float m_max_distance = 20.0f;

	uint64_t m_instance_id = 0;
	glm::vec3 m_last_position {0.f};
};

}
