/**
 * @file haptics_system.hpp
 * @author Xein
 * @date 22 Jun 2026
 */

#pragma once

#include <toast/assets/types.hpp>
#include <toast/events/listener.hpp>
#include <unordered_set>

namespace input {

class TOAST_API HapticsSystem {
public:
	HapticsSystem();
	~HapticsSystem();

	HapticsSystem(const HapticsSystem&) = delete;
	auto operator=(const HapticsSystem&) -> HapticsSystem& = delete;

	[[nodiscard]]
	static auto get() noexcept -> HapticsSystem&;

	/**
	 * @param controller SDL_JoystickID of the target controller; 0 = first/active gamepad
	 */
	void play(uint32_t controller, assets::AssetHandle<assets::Haptic> haptic);

	void setGlobalMultiplier(float multiplier) noexcept;

	[[nodiscard]]
	auto globalMultiplier() const noexcept -> float {
		return m_global_multiplier;
	}

	void tick();

private:
	struct Playback {
		uint32_t controller = 0;
		assets::AssetHandle<assets::Haptic> haptic;
		float elapsed = 0.0f;
	};

	static inline HapticsSystem* instance = nullptr;

	event::Listener m_listener;
	std::vector<Playback> m_playbacks;
	std::unordered_set<uint32_t> m_rumbling;
	float m_global_multiplier = 1.0f;
};

}
