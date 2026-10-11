#include "shader_cache.hpp"

#include "shader_compiler.hpp"

#include <cstdlib>
#include <cstring>
#include <format>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/thread_pool.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

/// Bump when the cached reflection shape or the compile options change since entries are keyed on the source hash
constexpr int k_cache_format = 7;

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

/// Source plus optional device features since TOAST_RAY_QUERY changes the SPIR-V
auto sourceHash(const std::string& source) -> uint64_t {
	return ShaderCache::fnv1a(source.data(), source.size()) ^ (ShaderCompiler::featureHash() * 0x100000001b3ull);
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
	ZoneScoped;
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

auto ShaderCache::sourceHandleLocked(toast::UID uid) -> assets::Handle<assets::Shader>& {
	auto [it, inserted] = m_sources.try_emplace(uid.data());
	if (inserted || !it->second.hasValue()) {
		it->second = assets::load<assets::Shader>(uid);
	}
	return it->second;
}

auto ShaderCache::sourceOf(toast::UID uid) -> std::optional<std::string> {
	const std::lock_guard lock(m_mutex);
	auto& handle = sourceHandleLocked(uid);
	if (!handle.hasValue()) {
		return std::nullopt;
	}
	return handle->source();
}

auto ShaderCache::isDiskCacheFresh(toast::UID uid, uint64_t source_hash) -> bool {
	nlohmann::json deps;
	{
		const std::lock_guard lock(m_mutex);
		loadHashIndexLocked();

		const auto& shaders = m_hash_index["shaders"];
		if (!shaders.is_object() || !shaders.contains(uid.get())) {
			return false;
		}

		const auto& entry = shaders[uid.get()];
		if (entry.value("hash", "") != hashToHex(source_hash)) {
			return false;
		}
		deps = entry.value("deps", nlohmann::json::object());
	}

	for (const auto& [dep_uri, dep_hash] : deps.items()) {
		auto dep_bytes = assets::AssetManager::get().tryLoadBytes(dep_uri);
		if (!dep_bytes || hashToHex(fnv1a(dep_bytes->data(), dep_bytes->size())) != dep_hash.get<std::string>()) {
			return false;
		}
	}

	return true;
}

auto ShaderCache::loadFromDisk(toast::UID uid) -> EntryPtr {
	ZoneScoped;
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

auto ShaderCache::compile(toast::UID uid, const std::string& source) -> EntryPtr {
	ZoneScoped;
	const std::string source_uri = assets::AssetManager::getURI(uid);

	auto compiled = ShaderCompiler::compile(uid, source, source_uri);
	if (compiled.spirv.empty()) {
		TOAST_ERROR("Render", "Compilation failed for shader {} ({})", uid.get(), source_uri);
		return nullptr;
	}

	auto entry = std::make_shared<Entry>();
	entry->spirv = std::move(compiled.spirv);
	entry->reflection = std::move(compiled.reflection);
	entry->hash = sourceHash(source);
	entry->source_uri = source_uri;
	entry->dependencies = std::move(compiled.dependencies);

	auto& manager = assets::AssetManager::get();

	std::vector<uint8_t> spirv_bytes(entry->spirv.size());
	std::memcpy(spirv_bytes.data(), entry->spirv.data(), entry->spirv.size());
	if (!manager.saveBytes(spirvUri(uid), spirv_bytes)) {
		TOAST_ERROR("Render", "Failed to write {}", spirvUri(uid));
	}

	const nlohmann::json cache_json {
	  {      "format",             k_cache_format},
	  {      "shader",                  uid.get()},
	  {      "source",          entry->source_uri},
	  {        "hash",     hashToHex(entry->hash)},
	  {"dependencies",        entry->dependencies},
	  {  "reflection", entry->reflection.toJson()},
	};
	const std::string cache_text = cache_json.dump(2);
	if (!manager.saveBytes(reflectionUri(uid), std::vector<uint8_t>(cache_text.begin(), cache_text.end()))) {
		TOAST_ERROR("Render", "Failed to write {}", reflectionUri(uid));
	}

	nlohmann::json deps_json = nlohmann::json::object();
	std::vector<uint64_t> dep_uids;
	for (const auto& dep_uri : entry->dependencies) {
		if (auto dep_bytes = manager.tryLoadBytes(dep_uri)) {
			deps_json[dep_uri] = hashToHex(fnv1a(dep_bytes->data(), dep_bytes->size()));
		}
		if (auto dep_uid = assets::AssetManager::resolveURI(dep_uri)) {
			dep_uids.push_back(dep_uid->data());
		}
	}

	{
		const std::lock_guard lock(m_mutex);
		loadHashIndexLocked();
		m_hash_index["shaders"][uid.get()] = {
		  {"hash", hashToHex(entry->hash)},
		  {"deps",              deps_json},
		};
		saveHashIndexLocked();

		for (const uint64_t dep_uid : dep_uids) {
			m_reverse_deps[dep_uid].insert(uid.data());
		}
	}

	TOAST_TRACE("Render", "Shader {} has {} dep(s)", uid.get(), entry->dependencies.size());
	TOAST_INFO("Render", "Compiled shader {} ({}) -> {} bytes of SPIR-V", uid.get(), source_uri, entry->spirv.size());
	return entry;
}

auto ShaderCache::loadOrCompile(toast::UID uid) -> EntryPtr {
	ZoneScoped;
	const auto source = sourceOf(uid);
	if (!source) {
		TOAST_ERROR("Render", "Unknown shader asset {}", uid.get());
		return nullptr;
	}

	if (isDiskCacheFresh(uid, sourceHash(*source))) {
		if (auto entry = loadFromDisk(uid)) {
			TOAST_TRACE("Render", "Recovered shader {} from disk cache", uid.get());
			return entry;
		}
	}

	return compile(uid, *source);
}

void ShaderCache::compileAllAtStartup() {
	ZoneScoped;
	const auto shader_uids = assets::listByType("shader");

	std::vector<std::future<bool>> jobs;
	jobs.reserve(shader_uids.size());
	for (const auto uid : shader_uids) {
		jobs.push_back(toast::ThreadPool::push([this, uid] {
			if (acquire(uid) != nullptr) {
				return true;
			}
			TOAST_ERROR("Render", "Failed to load or compile shader {}", uid.get());
			return false;
		}));
	}

	size_t loaded = 0;
	for (auto& job : jobs) {
		loaded += job.get() ? 1 : 0;
	}

	TOAST_INFO("Render", "Shader cache ready: {}/{} shaders loaded", loaded, shader_uids.size());
}

auto ShaderCache::acquire(toast::UID uid) -> EntryPtr {
	ZoneScoped;
	std::promise<EntryPtr> promise;
	std::shared_future<EntryPtr> running;
	{
		const std::lock_guard lock(m_mutex);
		if (const auto it = m_entries.find(uid.data()); it != m_entries.end()) {
			return it->second;
		}
		if (const auto it = m_in_flight.find(uid.data()); it != m_in_flight.end()) {
			running = it->second;
		} else {
			m_in_flight.emplace(uid.data(), promise.get_future().share());
		}
	}
	// The owner compiles inline rather than queueing so waiting here cannot starve the pool
	if (running.valid()) {
		return running.get();
	}

	EntryPtr entry;
	try {
		entry = loadOrCompile(uid);
	} catch (const std::exception& e) { TOAST_ERROR("Render", "Shader {} threw while compiling: {}", uid.get(), e.what()); }

	{
		const std::lock_guard lock(m_mutex);
		// A hot reload that landed meanwhile is newer
		if (entry != nullptr) {
			entry = m_entries.try_emplace(uid.data(), entry).first->second;
		}
		m_in_flight.erase(uid.data());
	}
	promise.set_value(entry);
	return entry;
}

auto ShaderCache::ensureCompiled(toast::UID uid) -> bool {
	return acquire(uid) != nullptr;
}

auto ShaderCache::onShaderSourceReloaded(toast::UID uid) -> bool {
	ZoneScoped;

	const auto recompile = [this](toast::UID target) -> bool {
		const auto source = sourceOf(target);
		auto entry = source ? compile(target, *source) : nullptr;
		if (!entry) {
			return false;
		}
		{
			const std::lock_guard lock(m_mutex);
			m_entries[target.data()] = std::move(entry);
		}
		event::send<event::ShaderRecompiled>(target);
		return true;
	};

	if (!recompile(uid)) {
		TOAST_WARN("Render", "Hot reload of shader {} failed, keeping previous SPIR-V", uid.get());
		return false;
	}

	std::vector<uint64_t> dependents;
	{
		const std::lock_guard lock(m_mutex);
		if (const auto it = m_reverse_deps.find(uid.data()); it != m_reverse_deps.end()) {
			dependents.assign(it->second.begin(), it->second.end());
		}
	}
	for (const uint64_t dependent : dependents) {
		if (!recompile(toast::UID(dependent))) {
			TOAST_WARN("Render", "Hot reload of dependent shader (of {}) failed, keeping previous SPIR-V", uid.get());
		}
	}

	return true;
}

}
