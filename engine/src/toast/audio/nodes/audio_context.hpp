/**
 * @file audio_context.hpp
 * @author Xein
 * @date 30 Jun 2026
 *
 * @brief Handles loading and unloading of Audio Banks
 */

#pragma once
#include <toast/audio/assets.hpp>
#include <toast/world/node.hpp>

struct FMOD_STUDIO_BANK;

namespace toast {

/**
 * @brief Handles loading and loading AudioBanks
 *
 * If you manually load or unload a bank during runtime (after init), it won't
 * be updated by default, and you need to call the @c reload() function
 */
class TOAST_API [[ToastNode, Color("Beige")]] AudioContext : public Node {
public:
	void addBank(const assets::Handle<assets::AudioBank>& bank);

	void removeBank(assets::Handle<assets::AudioBank> bank);

	[[Button("Reload Banks")]]
	void reload();

private:
	void init();

	void destroy();

	[[Reflect]]
	std::vector<assets::Handle<assets::AudioBank>> m_banks;
	std::mutex m_load_lock;

	[[Reflect, ReadOnly]]
	std::vector<std::string> m_events;

	std::vector<std::pair<assets::Handle<assets::AudioBank>, FMOD_STUDIO_BANK*>> m_loaded_banks;
};

}
