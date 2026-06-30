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

void AudioSystem::updateListenerAttributes(
    int id, glm::vec3 pos, glm::vec3 velocity, glm::vec3 forward, glm::vec3 up, std::optional<glm::vec3> attenuation_override
) {
	FMOD_3D_ATTRIBUTES attr = {};
	attr.position = { pos.x, pos.y, pos.z };
	attr.velocity = { velocity.x, velocity.y, velocity.z };
	attr.forward = { forward.x, forward.y, forward.z };
	attr.up = { up.x, up.y, up.z };
	
	
	FMOD_VECTOR atten = {};
	FMOD_VECTOR* atten_ptr = nullptr;
	if (attenuation_override) {
		atten = {attenuation_override->x, attenuation_override->y, attenuation_override->z};
		atten_ptr = &atten;
	}

	FMOD_Studio_System_SetListenerAttributes(m_system, id, &attr, atten_ptr);
}

void AudioSystem::updateListenerWeight(int id, float weight) {
	FMOD_Studio_System_SetListenerWeight(m_system, id, weight);
}

void AudioSystem::playEvent(std::string_view guidStr) {
	if (FMOD_STUDIO_EVENTINSTANCE* inst = getOrCreateInstance(guidStr)) {
		FMOD_Studio_EventInstance_Start(inst);
	}
}

void AudioSystem::stopEvent(std::string_view guidStr, bool smoothStop) {
	if (auto it = m_active_instances.find(std::string(guidStr)); it != m_active_instances.end()) {
		FMOD_STUDIO_STOP_MODE mode = smoothStop ? FMOD_STUDIO_STOP_ALLOWFADEOUT : FMOD_STUDIO_STOP_IMMEDIATE;
		FMOD_Studio_EventInstance_Stop(it->second, mode);
		FMOD_Studio_EventInstance_Release(it->second);
		m_active_instances.erase(it);
	}
}

void AudioSystem::pauseEvent(std::string_view guidStr, bool value) {
	if (auto it = m_active_instances.find(std::string(guidStr)); it != m_active_instances.end()) {
		FMOD_Studio_EventInstance_SetPaused(it->second, static_cast<FMOD_BOOL>(value));
	}
}

void AudioSystem::setParameter(std::string_view guidStr, std::string_view name, float value) {
	if (auto it = m_active_instances.find(std::string(guidStr)); it != m_active_instances.end()) {
		std::string null_terminated_name(name);
		FMOD_Studio_EventInstance_SetParameterByName(it->second, null_terminated_name.c_str(), value, false);
	}
}

void AudioSystem::setParameter(std::string_view guidStr, std::string_view name, bool value) {
	if (auto it = m_active_instances.find(std::string(guidStr)); it != m_active_instances.end()) {
		std::string null_terminated_name(name);
		float float_val = value ? 1.0f : 0.0f;
		FMOD_Studio_EventInstance_SetParameterByName(it->second, null_terminated_name.c_str(), float_val, false);
	}
}

void AudioSystem::setVolume(std::string_view guidStr, float volume) {
	if (auto it = m_active_instances.find(std::string(guidStr)); it != m_active_instances.end()) {
		FMOD_Studio_EventInstance_SetVolume(it->second, volume);
	}
}

void AudioSystem::setPitch(std::string_view guidStr, float pitch) {
	if (auto it = m_active_instances.find(std::string(guidStr)); it != m_active_instances.end()) {
		FMOD_Studio_EventInstance_SetPitch(it->second, pitch);
	}
}

auto AudioSystem::isEventPlaying(std::string_view guidStr) -> bool {
	if (auto it = m_active_instances.find(std::string(guidStr)); it != m_active_instances.end()) {
		FMOD_STUDIO_PLAYBACK_STATE state;
		if (FMOD_Studio_EventInstance_GetPlaybackState(it->second, &state) == FMOD_OK) {
			return (state == FMOD_STUDIO_PLAYBACK_PLAYING || state == FMOD_STUDIO_PLAYBACK_STARTING);
		}
	}
	return false;
}

auto AudioSystem::getOrCreateInstance(std::string_view guidStr) -> FMOD_STUDIO_EVENTINSTANCE* {
	// Lookups work without string allocations
	if (auto it = m_active_instances.find(std::string(guidStr)); it != m_active_instances.end()) {
		return it->second;
	}

	FMOD_STUDIO_EVENTDESCRIPTION* event_desc = nullptr;
	// Convert view to null-terminated string for FMOD C API
	std::string null_terminated_guid(guidStr);
	FMOD_RESULT result = FMOD_Studio_System_GetEvent(m_system, null_terminated_guid.c_str(), &event_desc);
	
	if (result != FMOD_OK || !event_desc) {
		TOAST_ERROR("AudioSystem", "Could not find FMOD event description for GUID: {}", guidStr);
		return nullptr;
	}

	FMOD_STUDIO_EVENTINSTANCE* instance_handle = nullptr;
	result = FMOD_Studio_EventDescription_CreateInstance(event_desc, &instance_handle);
	if (result != FMOD_OK || !instance_handle) {
		TOAST_ERROR("AudioSystem", "Failed to create FMOD instance for GUID: {}", guidStr);
		return nullptr;
	}

	m_active_instances[null_terminated_guid] = instance_handle;
	return instance_handle;
}

auto AudioSystem::playEvent3D(std::string_view guidStr) -> uint64_t {
    FMOD_STUDIO_EVENTDESCRIPTION* event_desc = nullptr;
    std::string null_terminated_guid(guidStr);
    
    if (FMOD_Studio_System_GetEvent(m_system, null_terminated_guid.c_str(), &event_desc) != FMOD_OK) {
        return 0; // Invalid ID
    }

    FMOD_STUDIO_EVENTINSTANCE* instance = nullptr;
    FMOD_Studio_EventDescription_CreateInstance(event_desc, &instance);
    
    if (instance) {
        uint64_t id = m_next_instance_id++;
        m_instances_3d[id] = instance;
        FMOD_Studio_EventInstance_Start(instance);
        return id;
    }
    return 0;
}

void AudioSystem::stopEvent3D(uint64_t instanceId, bool allowFadeout) {
    if (auto it = m_instances_3d.find(instanceId); it != m_instances_3d.end()) {
        FMOD_STUDIO_STOP_MODE mode = allowFadeout ? FMOD_STUDIO_STOP_ALLOWFADEOUT : FMOD_STUDIO_STOP_IMMEDIATE;
        FMOD_Studio_EventInstance_Stop(it->second, mode);
        FMOD_Studio_EventInstance_Release(it->second);
        m_instances_3d.erase(it);
    }
}

void AudioSystem::set3DAttributes(uint64_t instanceId, const glm::vec3& pos, const glm::vec3& vel, const glm::vec3& fwd, const glm::vec3& up) {
    if (auto it = m_instances_3d.find(instanceId); it != m_instances_3d.end()) {
        FMOD_3D_ATTRIBUTES attributes = {0};
        
        // Convert GLM to FMOD vectors (assuming right-handed Y-up; adjust if your engine differs)
        attributes.position = { pos.x, pos.y, pos.z };
        attributes.velocity = { vel.x, vel.y, vel.z };
        attributes.forward  = { fwd.x, fwd.y, fwd.z };
        attributes.up       = { up.x, up.y, up.z };

        FMOD_Studio_EventInstance_Set3DAttributes(it->second, &attributes);
    }
}

void AudioSystem::set3DMinMaxDistance(uint64_t instanceId, float minDistance, float maxDistance) {
    if (auto it = m_instances_3d.find(instanceId); it != m_instances_3d.end()) {
        FMOD_Studio_EventInstance_SetProperty(it->second, FMOD_STUDIO_EVENT_PROPERTY_MINIMUM_DISTANCE, minDistance);
        FMOD_Studio_EventInstance_SetProperty(it->second, FMOD_STUDIO_EVENT_PROPERTY_MAXIMUM_DISTANCE, maxDistance);
    }
}

void AudioSystem::pauseEvent(uint64_t instanceId, bool value) {
	if (auto it = m_instances_3d.find(instanceId); it != m_instances_3d.end()) {
		FMOD_Studio_EventInstance_SetPaused(it->second, static_cast<FMOD_BOOL>(value));
	}
}

void AudioSystem::setParameter(uint64_t instanceId, std::string_view name, float value) {
	if (auto it = m_instances_3d.find(instanceId); it != m_instances_3d.end()) {
		std::string null_terminated_name(name);
		FMOD_Studio_EventInstance_SetParameterByName(it->second, null_terminated_name.c_str(), value, false);
	}
}

void AudioSystem::setParameter(uint64_t instanceId, std::string_view name, bool value) {
	if (auto it = m_instances_3d.find(instanceId); it != m_instances_3d.end()) {
		std::string null_terminated_name(name);
		FMOD_Studio_EventInstance_SetParameterByName(it->second, null_terminated_name.c_str(), value ? 1.0f : 0.0f, false);
	}
}

void AudioSystem::setVolume(uint64_t instanceId, float volume) {
	if (auto it = m_instances_3d.find(instanceId); it != m_instances_3d.end()) {
		FMOD_Studio_EventInstance_SetVolume(it->second, volume);
	}
}

void AudioSystem::setPitch(uint64_t instanceId, float pitch) {
	if (auto it = m_instances_3d.find(instanceId); it != m_instances_3d.end()) {
		FMOD_Studio_EventInstance_SetPitch(it->second, pitch);
	}
}

auto AudioSystem::isEventPlaying(uint64_t instanceId) -> bool {
	if (auto it = m_instances_3d.find(instanceId); it != m_instances_3d.end()) {
		FMOD_STUDIO_PLAYBACK_STATE state;
		if (FMOD_Studio_EventInstance_GetPlaybackState(it->second, &state) == FMOD_OK) {
			return (state == FMOD_STUDIO_PLAYBACK_PLAYING || state == FMOD_STUDIO_PLAYBACK_STARTING);
		}
	}
	return false;
}

void AudioSystem::set3DOverrideAttenuation(uint64_t instanceId, float minDistance, float maxDistance) {
	if (auto it = m_instances_3d.find(instanceId); it != m_instances_3d.end()) {
		FMOD_Studio_EventInstance_SetProperty(it->second, FMOD_STUDIO_EVENT_PROPERTY_MINIMUM_DISTANCE, minDistance);
		FMOD_Studio_EventInstance_SetProperty(it->second, FMOD_STUDIO_EVENT_PROPERTY_MAXIMUM_DISTANCE, maxDistance);
	}
}

}

extern "C" {

void audio_generate_intermediates(const char* path) NOEXCEPT {
	std::filesystem::path {path};
	audio::AudioSystem::get().generateIntermediates(path);
}
}
