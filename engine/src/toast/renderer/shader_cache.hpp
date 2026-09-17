/**
 * @file shader_cache.hpp
 * @author Xein
 * @date 17 Jul 2026
 */

#pragma once

#include "shader_reflection.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <toast/assets/core_types.hpp>
#include <toast/assets/shader.hpp>
#include <toast/events/event.hpp>
#include <toast/events/listener.hpp>
#include <toast/uid.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace event {

struct ShaderRecompiled : public Event<ShaderRecompiled> {
	toast::UID uid;

	explicit ShaderRecompiled(toast::UID uid) : uid(uid) { }
};

}

namespace renderer {

class TOAST_API ShaderCache {
public:
	struct Entry {
		std::vector<std::byte> spirv;
		ShaderReflection reflection;
		uint64_t hash = 0;
		std::string source_uri;
		std::vector<std::string> dependencies;
	};

	static auto get() -> ShaderCache&;

	void compileAllAtStartup();

	auto acquire(toast::UID uid) -> std::shared_ptr<const Entry>;

	auto ensureCompiled(toast::UID uid) -> bool;

	auto onShaderSourceReloaded(toast::UID uid) -> bool;

	static auto fnv1a(const void* data, size_t size) -> uint64_t;

private:
	ShaderCache();

	auto loadOrCompileLocked(toast::UID uid) -> std::shared_ptr<const Entry>;
	auto compileLocked(toast::UID uid) -> std::shared_ptr<const Entry>;
	auto loadFromDiskLocked(toast::UID uid) -> std::shared_ptr<const Entry>;
	auto isDiskCacheFreshLocked(toast::UID uid, uint64_t source_hash) -> bool;
	void loadHashIndexLocked();
	void saveHashIndexLocked();
	auto sourceHandleLocked(toast::UID uid) -> assets::Handle<assets::Shader>&;

	std::mutex m_mutex;
	std::unordered_map<uint64_t, std::shared_ptr<const Entry>> m_entries;
	std::unordered_map<uint64_t, assets::Handle<assets::Shader>> m_sources;
	std::unordered_map<uint64_t, std::unordered_set<uint64_t>> m_reverse_deps;
	nlohmann::json m_hash_index;
	bool m_hash_index_loaded = false;

	event::Listener m_listener;
};

}
