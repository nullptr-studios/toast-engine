#include "audio_context.hpp"

#include "../audio_system.hpp"

namespace toast {
void AudioContext::addBank(const assets::Handle<assets::AudioBank>& bank) {
	ZoneScoped;
	TOAST_INFO("Audio", "Added bank to AudioContext {}", box());
	std::scoped_lock lock(m_load_lock);
	m_banks.emplace_back(bank);
}

void AudioContext::removeBank(assets::Handle<assets::AudioBank> bank) {
	ZoneScoped;
	TOAST_INFO("Audio", "Removed bank from AudioContext {}", box());
	std::scoped_lock lock(m_load_lock);

	// Also remove from System if it was already loaded
	if (auto it = std::ranges::find_if(m_loaded_banks, [bank](const auto& v) { return v.first == bank; });
	    it != m_loaded_banks.end()) {
		audio::AudioSystem::get().unloadBank(it->second);
		m_loaded_banks.erase(it);
	}

	std::erase(m_banks, bank);
}

void AudioContext::reload() {
	ZoneScoped;
	TOAST_INFO("Audio", "Reloading {} banks on {}", m_banks.size(), box());
	std::scoped_lock lock(m_load_lock);

	TOAST_TRACE("Audio", "Unloading {} banks", m_loaded_banks.size());
	for (auto* bank : m_loaded_banks | std::views::values) {
		audio::AudioSystem::get().unloadBank(bank);
	}
	m_loaded_banks.clear();
	m_events.clear();

	for (const auto& b : m_banks) {
		auto [fmod_bank, events] = audio::AudioSystem::get().loadBank(b);
		m_loaded_banks.emplace_back(b, fmod_bank);
		std::ranges::copy(events, std::back_inserter(m_events));
	}
	TOAST_TRACE("Audio", "Loaded {} banks", m_loaded_banks.size());
}

void AudioContext::init() {
	std::scoped_lock lock(m_load_lock);
	for (const auto& b : m_banks) {
		auto [fmod_bank, events] = audio::AudioSystem::get().loadBank(b);
		m_loaded_banks.emplace_back(b, fmod_bank);
		std::ranges::copy(events, std::back_inserter(m_events));
	}

	TOAST_INFO("Audio", "Loaded {} banks to {}", m_loaded_banks.size(), box());
}

void AudioContext::destroy() {
	std::scoped_lock lock(m_load_lock);
	TOAST_INFO("Audio", "Unloaded {} banks from {}", m_loaded_banks.size(), box());
	for (auto* bank : m_loaded_banks | std::views::values) {
		audio::AudioSystem::get().unloadBank(bank);
	}
	m_loaded_banks.clear();
}
}
