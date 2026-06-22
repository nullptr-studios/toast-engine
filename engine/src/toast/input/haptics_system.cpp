#include "haptics_system.hpp"

#include "input_events.hpp"
#include "input_system.hpp"

#include <SDL3/SDL_gamepad.h>
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <toast/events/event.hpp>
#include <toast/log.hpp>
#include <unordered_map>

namespace input {

namespace {

constexpr uint32_t rumble_slack_ms = 50;

auto toRumble(float value) -> uint16_t {
	return static_cast<uint16_t>(std::clamp(value, 0.0f, 1.0f) * 65535.0f);
}

}

HapticsSystem::HapticsSystem() {
	instance = this;

	m_listener.subscribe<event::SetHapticsMultiplier>([this](const event::SetHapticsMultiplier& e) {
		setGlobalMultiplier(e.multiplier);
		return false;
	});

	TOAST_INFO("Haptics", "Haptics system created");
}

HapticsSystem::~HapticsSystem() {
	if (instance == this) {
		instance = nullptr;
	}
}

auto HapticsSystem::get() noexcept -> HapticsSystem& {
	return *instance;
}

void HapticsSystem::play(uint32_t controller, assets::AssetHandle<assets::Haptic> haptic) {
	if (!haptic.hasValue()) {
		TOAST_WARN("Haptics", "Ignoring play request with an unresolved haptic asset");
		return;
	}

	if (controller == 0) {
		controller = InputSystem::get().activeGamepadId();
	}
	if (controller == 0) {
		TOAST_WARN("Haptics", "No controller available to play haptic");
		return;
	}

	m_playbacks.push_back(Playback {.controller = controller, .haptic = std::move(haptic), .elapsed = 0.0f});
}

void HapticsSystem::setGlobalMultiplier(float multiplier) noexcept {
	m_global_multiplier = std::max(0.0f, multiplier);
}

void HapticsSystem::tick() {
	// Advance every effect and drop the ones whose duration has elapsed
	for (auto& pb : m_playbacks) {
		pb.elapsed += Time::delta();
	}
	std::erase_if(m_playbacks, [](const Playback& pb) {
		if (!pb.haptic.hasValue()) {
			return true;
		}
		const float duration = pb.haptic->durationMs() / 1000.0f;
		return pb.elapsed >= duration;
	});

	// highest priority wins, ties take the per-motor max
	struct Blend {
		int priority = 0;
		glm::vec2 motors {0.0f};
	};

	std::unordered_map<uint32_t, Blend> per_controller;

	for (const auto& pb : m_playbacks) {
		const assets::Haptic& haptic = *pb.haptic;
		const float duration = haptic.durationMs() / 1000.0f;
		const float t01 = duration > 0.0f ? std::clamp(pb.elapsed / duration, 0.0f, 1.0f) : 0.0f;
		const glm::vec2 motors = haptic.sample(t01) * m_global_multiplier;
		const int priority = haptic.priority();

		auto it = per_controller.find(pb.controller);
		if (it == per_controller.end()) {
			per_controller.emplace(pb.controller, Blend {.priority = priority, .motors = motors});
		} else if (priority > it->second.priority) {
			it->second = Blend {.priority = priority, .motors = motors};
		} else if (priority == it->second.priority) {
			it->second.motors = glm::max(it->second.motors, motors);
		}
	}

	const uint32_t slice = static_cast<uint32_t>(Time::delta() * 1000.0f) + rumble_slack_ms;
	std::unordered_set<uint32_t> active_now;
	for (const auto& [controller, blend] : per_controller) {
		SDL_Gamepad* pad = SDL_GetGamepadFromID(controller);
		if (pad == nullptr) {
			continue;
		}
		SDL_RumbleGamepad(pad, toRumble(blend.motors.x), toRumble(blend.motors.y), slice);
		active_now.insert(controller);
	}

	// Stop controllers that were rumbling last frame but have no effect now
	for (uint32_t controller : m_rumbling) {
		if (!active_now.contains(controller)) {
			if (SDL_Gamepad* pad = SDL_GetGamepadFromID(controller)) {
				SDL_RumbleGamepad(pad, 0, 0, 0);
			}
		}
	}
	m_rumbling = std::move(active_now);
}

}
