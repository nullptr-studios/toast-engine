#include "shader_cache.hpp"

#include "shader_compiler.hpp"

#include <cstdlib>
#include <cstring>
#include <format>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>

namespace renderer {

namespace {

constexpr int k_cache_format = 1;

auto spirvUri(toast::UID uid) -> std::string {
	return "cache://shaders/" + uid.get() + ".spv";
}

auto reflectionUri(toast::UID uid) -> std::string {
	return "cache://shaders/" + uid.get() + ".json";
}

constexpr std::string_view k_hash_index_uri = "cache://shaders/hash.json";

auto hashToHex(uint64_t hash) -> std::string {
	return std::format("{:016x}", hash);
}

}

ShaderCache::ShaderCache() {
	m_listener.subscribe<event::ShaderAssetReloaded>([this](const event::ShaderAssetReloaded& e) {
		onShaderSourceReloaded(e.uid);
		return false;
	});
}

auto ShaderCache::get() -> ShaderCache& {
	static ShaderCache instance;
	return instance;
}

auto ShaderCache::fnv1a(const void* data, size_t size) -> uint64_t {
	constexpr uint64_t k_offset_basis = 0xcbf29ce484222325ull;
	constexpr uint64_t k_prime = 0x100000001b3ull;

	const auto* bytes = static_cast<const uint8_t*>(data);
	uint64_t hash = k_offset_basis;
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= k_prime;
	}
	return hash;
}

void ShaderCache::loadHashIndexLocked() {
	if (m_hash_index_loaded) {
		return;
	}
	m_hash_index_loaded = true;
	m_hash_index = nlohmann::json::object();

	auto bytes = assets::AssetManager::get().tryLoadBytes(k_hash_index_uri);
	if (!bytes) {
		return;
	}

	auto parsed = nlohmann::json::parse(bytes->begin(), bytes->end(), nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object() || parsed.value("format", 0) != k_cache_format) {
		TOAST_WARN("Render", "Discarding unreadable or outdated shader hash at {}", k_hash_index_uri);
		return;
	}
	m_hash_index = std::move(parsed);
}

void ShaderCache::saveHashIndexLocked() {
	m_hash_index["format"] = k_cache_format;
	const std::string text = m_hash_index.dump(2);
	const std::vector<uint8_t> bytes(text.begin(), text.end());
	if (!assets::AssetManager::get().saveBytes(k_hash_index_uri, bytes)) {
		TOAST_ERROR("Render", "Failed to write shader hash {}", k_hash_index_uri);
	}
}

auto ShaderCache::sourceHandleLocked(toast::UID uid) -> assets::AssetHandle<assets::Shader>& {
	auto [it, inserted] = m_sources.try_emplace(uid.data());
	if (inserted || !it->second.hasValue()) {
		it->second = assets::load<assets::Shader>(uid);
	}
	return it->second;
}

auto ShaderCache::isDiskCacheFreshLocked(toast::UID uid, uint64_t source_hash) -> bool {
	loadHashIndexLocked();

	const auto& shaders = m_hash_index["shaders"];
	if (!shaders.is_object() || !shaders.contains(uid.get())) {
		return false;
	}

	const auto& entry = shaders[uid.get()];
	if (entry.value("hash", "") != hashToHex(source_hash)) {
		return false;
	}

	// Any changed or missing dependency invalidates the cache
	for (const auto& [dep_uri, dep_hash] : entry.value("deps", nlohmann::json::object()).items()) {
		auto dep_bytes = assets::AssetManager::get().tryLoadBytes(dep_uri);
		if (!dep_bytes || hashToHex(fnv1a(dep_bytes->data(), dep_bytes->size())) != dep_hash.get<std::string>()) {
			return false;
		}
	}

	return true;
}

auto ShaderCache::loadFromDiskLocked(toast::UID uid) -> std::shared_ptr<const Entry> {
	auto& manager = assets::AssetManager::get();

	auto spirv_bytes = manager.tryLoadBytes(spirvUri(uid));
	auto json_bytes = manager.tryLoadBytes(reflectionUri(uid));
	if (!spirv_bytes || spirv_bytes->empty() || !json_bytes) {
		return nullptr;
	}

	auto parsed = nlohmann::json::parse(json_bytes->begin(), json_bytes->end(), nullptr, false);
	if (parsed.is_discarded() || parsed.value("format", 0) != k_cache_format) {
		return nullptr;
	}

	auto reflection = ShaderReflection::fromJson(parsed.value("reflection", nlohmann::json::object()));
	if (!reflection) {
		return nullptr;
	}

	auto entry = std::make_shared<Entry>();
	entry->spirv.resize(spirv_bytes->size());
	std::memcpy(entry->spirv.data(), spirv_bytes->data(), spirv_bytes->size());
	entry->reflection = std::move(*reflection);
	entry->hash = std::strtoull(parsed.value("hash", "0").c_str(), nullptr, 16);
	entry->source_uri = parsed.value("source", "");
	entry->dependencies = parsed.value("dependencies", std::vector<std::string> {});

	return entry;
}

auto ShaderCache::compileLocked(toast::UID uid) -> std::shared_ptr<const Entry> {
	auto& source_handle = sourceHandleLocked(uid);
	if (!source_handle.hasValue()) {
		TOAST_ERROR("Render", "Cannot compile shader {}: asset not found", uid.get());
		return nullptr;
	}

	const std::string source_uri = assets::AssetManager::getURI(uid);
	const std::string& source = source_handle->source();

	auto compiled = ShaderCompiler::compile(uid, source, source_uri);
	if (compiled.spirv.empty()) {
		TOAST_ERROR("Render", "Compilation failed for shader {} ({})", uid.get(), source_uri);
		return nullptr;
	}

	auto entry = std::make_shared<Entry>();
	entry->spirv = std::move(compiled.spirv);
	entry->reflection = std::move(compiled.reflection);
	entry->hash = fnv1a(source.data(), source.size());
	entry->source_uri = source_uri;
	entry->dependencies = std::move(compiled.dependencies);

	auto& manager = assets::AssetManager::get();

	// SPIR-V blob
	std::vector<uint8_t> spirv_bytes(entry->spirv.size());
	std::memcpy(spirv_bytes.data(), entry->spirv.data(), entry->spirv.size());
	if (!manager.saveBytes(spirvUri(uid), spirv_bytes)) {
		TOAST_ERROR("Render", "Failed to write {}", spirvUri(uid));
	}

	// Reflection + metadata json
	nlohmann::json deps_json = nlohmann::json::object();
	nlohmann::json cache_json {
	  {	    "format",             k_cache_format},
	  {	    "shader",	                uid.get()},
	  {	    "source",          entry->source_uri},
	  {	      "hash",     hashToHex(entry->hash)},
	  {"dependencies",        entry->dependencies},
	  {  "reflection", entry->reflection.toJson()},
	};
	const std::string cache_text = cache_json.dump(2);
	if (!manager.saveBytes(reflectionUri(uid), std::vector<uint8_t>(cache_text.begin(), cache_text.end()))) {
		TOAST_ERROR("Render", "Failed to write {}", reflectionUri(uid));
	}

	// Hash index entry, with current hashes for every dependency
	loadHashIndexLocked();
	for (const auto& dep_uri : entry->dependencies) {
		if (auto dep_bytes = manager.tryLoadBytes(dep_uri)) {
			deps_json[dep_uri] = hashToHex(fnv1a(dep_bytes->data(), dep_bytes->size()));
		}
	}
	m_hash_index["shaders"][uid.get()] = {
	  {"hash", hashToHex(entry->hash)},
	  {"deps",	            deps_json},
	};
	saveHashIndexLocked();

	for (const auto& dep_uri : entry->dependencies) {
		if (auto dep_uid = assets::AssetManager::resolveURI(dep_uri)) {
			m_reverse_deps[dep_uid->data()].insert(uid.data());
		}
	}

	TOAST_TRACE("Render", "Shader {} has {} dep(s)", uid.get(), entry->dependencies.size());
	TOAST_INFO("Render", "Compiled shader {} ({}) -> {} bytes of SPIR-V", uid.get(), source_uri, entry->spirv.size());
	return entry;
}

auto ShaderCache::loadOrCompileLocked(toast::UID uid) -> std::shared_ptr<const Entry> {
	auto& source_handle = sourceHandleLocked(uid);
	if (!source_handle.hasValue()) {
		TOAST_ERROR("Render", "Unknown shader asset {}", uid.get());
		return nullptr;
	}

	const std::string& source = source_handle->source();
	const uint64_t source_hash = fnv1a(source.data(), source.size());

	if (isDiskCacheFreshLocked(uid, source_hash)) {
		if (auto entry = loadFromDiskLocked(uid)) {
			TOAST_TRACE("Render", "Recovered shader {} from disk cache", uid.get());
			return entry;
		}
	}

	return compileLocked(uid);
}

void ShaderCache::compileAllAtStartup() {
	const auto shader_uids = assets::listByType("shader");

	std::lock_guard lock(m_mutex);
	size_t compiled = 0;
	for (const auto uid : shader_uids) {
		if (m_entries.contains(uid.data())) {
			continue;
		}
		if (auto entry = loadOrCompileLocked(uid)) {
			m_entries[uid.data()] = std::move(entry);
			++compiled;
		} else {
			TOAST_ERROR("Render", "Failed to load or compile shader {}", uid.get());
		}
	}

	TOAST_INFO("Render", "Shader cache ready: {}/{} shaders loaded", compiled, shader_uids.size());
}

auto ShaderCache::acquire(toast::UID uid) -> std::shared_ptr<const Entry> {
	std::lock_guard lock(m_mutex);

	if (const auto it = m_entries.find(uid.data()); it != m_entries.end()) {
		return it->second;
	}

	auto entry = loadOrCompileLocked(uid);
	if (entry) {
		m_entries[uid.data()] = entry;
	}
	return entry;
}

auto ShaderCache::ensureCompiled(toast::UID uid) -> bool {
	return acquire(uid) != nullptr;
}

auto ShaderCache::onShaderSourceReloaded(toast::UID uid) -> bool {
	std::lock_guard lock(m_mutex);

	auto entry = compileLocked(uid);
	if (!entry) {
		// Keep the last-good entry so the renderer can keep drawing
		TOAST_WARN("Render", "Hot reload of shader {} failed, keeping previous SPIR-V", uid.get());
		return false;
	}

	m_entries[uid.data()] = std::move(entry);
	event::send<event::ShaderRecompiled>(uid);

	const auto it = m_reverse_deps.find(uid.data());
	if (it != m_reverse_deps.end()) {
		for (const auto dep_hash : it->second) {
			auto dep_entry = compileLocked(toast::UID(dep_hash));
			if (dep_entry) {
				m_entries[dep_hash] = std::move(dep_entry);
				event::send<event::ShaderRecompiled>(toast::UID(dep_hash));
			} else {
				TOAST_WARN("Render", "Hot reload of dependent shader (of {}) failed, keeping previous SPIR-V", uid.get());
			}
		}
	}

	return true;
}

}
