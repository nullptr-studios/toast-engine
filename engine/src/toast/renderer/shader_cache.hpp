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

/**
 * @brief Fired by the ShaderCache after a hot-reloaded shader finished recompiling
 */
struct ShaderRecompiled : public Event<ShaderRecompiled> {
	toast::UID uid;

	explicit ShaderRecompiled(toast::UID uid) : uid(uid) { }
};

}

namespace renderer {

/**
 * @class ShaderCache
 * @brief In-memory and on-disk cache of compiled SPIR-V shaders
 */
class TOAST_API ShaderCache {
public:
	struct Entry {
		std::vector<std::byte> spirv;
		ShaderReflection reflection;
		uint64_t hash = 0;                        ///< FNV-1a of the source bytes
		std::string source_uri;                   ///< virtual URI of the .slang source
		std::vector<std::string> dependencies;    ///< virtual URIs of imported files
	};

	static auto get() -> ShaderCache&;

	/**
	 * Compiles every @c shader asset in the manifest that is missing or stale in the
	 * disk cache, loads everything else from disk, and keeps all SPIRV in memory
	 */
	void compileAllAtStartup();

	/**
	 * @returns the cached entry for a shader, compiling or loading from disk on a miss
	 */
	auto acquire(toast::UID uid) -> std::shared_ptr<const Entry>;

	auto ensureCompiled(toast::UID uid) -> bool;    ///< Makes sure the cache is fresh

	/**
	 * Recompiles a shader whose source changed on disk; keeps the last-good entry when
	 * compilation fails. Returns true when a new entry was stored
	 */
	auto onShaderSourceReloaded(toast::UID uid) -> bool;

	static auto fnv1a(const void* data, size_t size) -> uint64_t;    ///< file hasher

private:
	ShaderCache();

	auto loadOrCompileLocked(toast::UID uid) -> std::shared_ptr<const Entry>;
	auto compileLocked(toast::UID uid) -> std::shared_ptr<const Entry>;
	auto loadFromDiskLocked(toast::UID uid) -> std::shared_ptr<const Entry>;
	auto isDiskCacheFreshLocked(toast::UID uid, uint64_t source_hash) -> bool;
	void loadHashIndexLocked();
	void saveHashIndexLocked();
	auto sourceHandleLocked(toast::UID uid) -> assets::AssetHandle<assets::Shader>&;

	std::mutex m_mutex;
	std::unordered_map<uint64_t, std::shared_ptr<const Entry>> m_entries;
	std::unordered_map<uint64_t, assets::AssetHandle<assets::Shader>> m_sources;
	std::unordered_map<uint64_t, std::unordered_set<uint64_t>> m_reverse_deps;
	nlohmann::json m_hash_index;
	bool m_hash_index_loaded = false;

	event::Listener m_listener;
};

}
