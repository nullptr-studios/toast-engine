#include "audio_system.hpp"

#include <ffi/audio.h>
#include <filesystem>
#include <fmod/fmod_errors.h>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/core_types.hpp>
#include <toast/log.hpp>

namespace {

#ifdef DEBUG
auto fmodLogCallback(FMOD_DEBUG_FLAGS flags, const char* file, int line, const char* func, const char* message) -> FMOD_RESULT {
	std::string msg = std::format("{}: {}", func, message);
	uint8_t severity = 0;
	if (flags & FMOD_DEBUG_LEVEL_WARNING) {
		severity = 2;
	}
	if (flags & FMOD_DEBUG_LEVEL_ERROR) {
		severity = 3;
	}

	std::string path = file;
	size_t pos = std::max(path.find_last_of('/'), path.find_last_of('\\'));
	std::string name = pos == std::string::npos ? path : path.substr(pos + 1);

	::logging::_detail::log(severity, path, line, "Audio", msg);

	constexpr uint32_t red = 0xFF0000;
	constexpr uint32_t yellow = 0x00FFFF;
	switch (severity) {
		case 3: TracyMessageC(msg.c_str(), msg.size(), red); break;
		case 2: TracyMessageC(msg.c_str(), msg.size(), yellow); break;
		default: break;
	}
	return FMOD_OK;
}
#endif

std::string GuidToString(const FMOD_GUID& guid) {
	char buffer[64];
	// FMOD_GUID: Data1 (uint), Data2 (ushort), Data3 (ushort), Data4 (char[8])
	std::snprintf(
	    buffer,
	    sizeof(buffer),
	    "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
	    guid.Data1,
	    guid.Data2,
	    guid.Data3,
	    guid.Data4[0],
	    guid.Data4[1],
	    guid.Data4[2],
	    guid.Data4[3],
	    guid.Data4[4],
	    guid.Data4[5],
	    guid.Data4[6],
	    guid.Data4[7]
	);

	return std::string(buffer);
}

}

namespace audio {

AudioSystem::AudioSystem() noexcept {
	TOAST_ASSERT(not instance, "Audio", "An AudioSystem class already exists");

	instance = this;
	auto result = FMOD_Studio_System_Create(&m_system, FMOD_VERSION);
	TOAST_ASSERT(result == FMOD_OK, "Audio", "FMOD could not be started: {}", FMOD_ErrorString(result));

	FMOD_Studio_System_GetCoreSystem(m_system, &m_core_system);

#ifdef DEBUG
	FMOD_Debug_Initialize(
	    FMOD_DEBUG_LEVEL_LOG | FMOD_DEBUG_LEVEL_WARNING | FMOD_DEBUG_LEVEL_ERROR, FMOD_DEBUG_MODE_CALLBACK, fmodLogCallback, nullptr
	);
#endif

	int studio_init_flags = FMOD_STUDIO_INIT_NORMAL;
#ifdef DEBUG
	studio_init_flags |= FMOD_STUDIO_INIT_LIVEUPDATE;
#endif

	result = FMOD_Studio_System_Initialize(m_system, 512, studio_init_flags, FMOD_INIT_NORMAL, nullptr);
	TOAST_ASSERT(result == FMOD_OK, "Audio", "FMOD could not be started: {}", FMOD_ErrorString(result));

	TOAST_INFO("Audio", "FMOD instance created");
}

AudioSystem::~AudioSystem() noexcept {
	FMOD_Studio_System_Release(m_system);
	m_system = nullptr;
	instance = nullptr;
	TOAST_INFO("Audio", "FMOD instance destroyed");
}

auto AudioSystem::get() noexcept -> AudioSystem& {
	TOAST_ASSERT(instance, "Audio", "AudioSystem does not exist yet");
	return *instance;
}

void AudioSystem::tick() const noexcept {
	FMOD_Studio_System_Update(m_system);
}

void AudioSystem::generateIntermediates(const std::filesystem::path& path) {
	FMOD_STUDIO_BANK* bank = nullptr;

	FMOD_Studio_System_LoadBankFile(m_system, path.string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank);

	FMOD_STUDIO_LOADING_STATE state = FMOD_STUDIO_LOADING_STATE_UNLOADED;

	while (state != FMOD_STUDIO_LOADING_STATE_LOADED) {
		FMOD_Studio_Bank_GetLoadingState(bank, &state);
		FMOD_Studio_System_Update(m_system);

		if (state == FMOD_STUDIO_LOADING_STATE_ERROR) {
			TOAST_ERROR("Audio", "Failed reading Master.strings.bank");
			return;
		}
	}

	json_t json;

	// Initialize empty objects so they exist in the JSON even if empty
	json["banks"] = json_t::object();
	json["events"] = json_t::object();
	json["snapshots"] = json_t::object();
	json["buses"] = json_t::object();
	json["vcas"] = json_t::object();
	json["parameters"] = json_t::object();
	json["ports"] = json_t::object();

	int string_count = 0;
	FMOD_Studio_Bank_GetStringCount(bank, &string_count);

	for (int i = 0; i < string_count; ++i) {
		FMOD_GUID guid;
		std::array<char, 256> path_buffer;
		int retrieved = 0;

		FMOD_Studio_Bank_GetStringInfo(bank, i, &guid, path_buffer.data(), path_buffer.size(), &retrieved);

		std::string path_str(path_buffer.data(), strnlen(path_buffer.data(), retrieved));
		// while (!path_str.empty() && (path_str.back() == '\0' || path_str.back() == '.')) {
		// 	path_str.pop_back();
		// }
		std::string guid_str = GuidToString(guid);

		// Sort into the correct JSON category based on the FMOD path prefix
		if (path_str.find("event:/") == 0) {
			json["events"][guid_str] = path_str;
		} else if (path_str.find("snapshot:/") == 0) {
			json["snapshots"][guid_str] = path_str;
		} else if (path_str.find("bus:/") == 0) {
			json["buses"][guid_str] = path_str;
		} else if (path_str.find("vca:/") == 0) {
			json["vcas"][guid_str] = path_str;
		} else if (path_str.find("bank:/") == 0) {
			json["banks"][guid_str] = path_str;
		} else if (path_str.find("port:/") == 0) {
			json["ports"][guid_str] = path_str;
		}
	}

	FMOD_Studio_Bank_Unload(bank);

	std::filesystem::path cache_dir = assets::AssetManager::get().getCachePath() / "fmod";
	std::filesystem::create_directories(cache_dir);
	std::ofstream file {cache_dir / "audio.json"};
	if (not file) {
		TOAST_ERROR("Audio", "Failed to create audio.json");
		return;
	}
	file << json.dump(4);
}

auto AudioSystem::loadBank(assets::AssetHandle<assets::AudioBank> bank) const
    -> std::pair<FMOD_STUDIO_BANK*, std::vector<std::string>> {
	if (not bank.hasValue() || bank->get().size() == 0) {
		TOAST_WARN("Audio", "Tried to load empty bank");
		return {nullptr, {}};
	}

	FMOD_STUDIO_BANK* fmod_bank = nullptr;
	FMOD_Studio_System_LoadBankMemory(
		m_system,
		reinterpret_cast<const char*>(bank->get().data()),
		bank->get().size(), 
		FMOD_STUDIO_LOAD_MEMORY ,
		FMOD_STUDIO_LOAD_BANK_NORMAL,
		&fmod_bank
	);
	
	int event_count;
	FMOD_Studio_Bank_GetEventCount(fmod_bank, &event_count);
	std::vector<FMOD_STUDIO_EVENTDESCRIPTION*> events(event_count);
	FMOD_Studio_Bank_GetEventList(fmod_bank, events.data(), event_count, &event_count);
	std::vector<std::string> event_paths;
	for (auto* desc : events) {
		std::array<char, 512> path;
		int retrieved = 0;
		FMOD_Studio_EventDescription_GetPath(desc, path.data(), path.size(), &retrieved);
		event_paths.emplace_back(path.data(), retrieved);
	}
	
	return {fmod_bank, event_paths};
}

void AudioSystem::unloadBank(FMOD_STUDIO_BANK* bank) const {
	FMOD_Studio_Bank_Unload(bank);
}

}

extern "C" {

void audio_generate_intermediates(const char* path) NOEXCEPT {
	std::filesystem::path {path};
	audio::AudioSystem::get().generateIntermediates(path);
}
}
