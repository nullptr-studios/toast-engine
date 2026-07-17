#include "asset_manager.hpp"

#include "asset_registry.hpp"
#include "prefab.hpp"
#include "script.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <toast/log.hpp>
#include <toast/project_settings.hpp>

namespace assets {

void AssetManager::setLoadMode(SaveMode mode) {
	load_mode = mode;
	TOAST_INFO("AssetManager", "Load mode set to {}", mode == SaveMode::game ? "game" : "editor");
}

auto AssetManager::getLoadMode() -> SaveMode {
	return load_mode;
}

void AssetManager::mountPack(std::string_view scheme, const std::filesystem::path& pak_path) {
	try {
		mounts.emplace(std::string(scheme), std::make_unique<PackArchive>(pak_path));
		TOAST_INFO("AssetManager", "Mounted pack '{}://' → {}", scheme, pak_path.string());
	} catch (const std::exception& e) { TOAST_ERROR("AssetManager", "Failed to mount pack '{}': {}", scheme, e.what()); }
}

AssetManager::AssetManager() {
	instance = this;

	AssetRegistry::init();
	reloadManifest();

	listener.subscribe<event::ReloadAssetsManifest>([this] { reloadManifest(); });
	listener.subscribe<event::ClearUnusedAssets>([this] { clearUnusedAssets(); });
}

auto AssetManager::get() noexcept -> AssetManager& {
	TOAST_ASSERT(instance != nullptr, "AssetManager", "AssetManager instance is not initialized");
	return *instance;
}

auto AssetManager::load(toast::UID uid) -> Asset* {
	ZoneScoped;
	uint64_t id = uid.data();

	// empty asset
	if (id == 0) {
		return nullptr;
	}

	std::lock_guard lock(mutex);

	// Check cache
	if (auto it = cache.find(id); it != cache.end()) {
		return it->second.get();
	}

	// Check manifest
	auto manifest_it = manifest.find(id);
	if (manifest_it == manifest.end()) {
		TOAST_ERROR("AssetManager", "Asset with UID {} not found in manifest", uid);
		return nullptr;
	}

	const auto& info = manifest_it->second;
	auto real_path = resolveVirtualPath(info.path);
	if (!real_path) {
		TOAST_ERROR("AssetManager", "Could not resolve virtual path: {}", info.path);
		return nullptr;
	}

	auto raw_data = readVirtualPath(info.path);
	if (!raw_data) {
		return nullptr;
	}

	std::unique_ptr<Asset> asset = nullptr;

	auto resolve_schema = [&](const toml::table& table) -> AssetHandle<Schema> {
		AssetHandle<Schema> schema_handle;
		if (const auto* schema_key = table.get("schema")) {
			if (auto schema_uid_str = schema_key->value<std::string_view>()) {
				if (schema_uid_str->size() == 11) {
					toast::UID schema_uid(toast::UID::fromString(*schema_uid_str));
					if (auto it = manifest.find(schema_uid.data()); it != manifest.end()) {
						{
							if (auto schema_raw = readVirtualPath(it->second.path)) {
								if (auto cit = cache.find(schema_uid.data()); cit != cache.end()) {
									schema_handle = AssetHandle<Schema>(static_cast<Schema*>(cit->second.get()), schema_uid, getURI(schema_uid));
								} else {
									std::string_view schema_json(reinterpret_cast<const char*>(schema_raw->data()), schema_raw->size());
									try {
										auto schema_asset = std::make_unique<Schema>(schema_json);
										Schema* raw_ptr = schema_asset.get();
										cache[schema_uid.data()] = std::move(schema_asset);
										schema_handle = AssetHandle<Schema>(raw_ptr, schema_uid, getURI(schema_uid));
									} catch (const std::exception& se) {
										TOAST_WARN("AssetManager", "Could not parse schema for asset {}: {}", info.path, se.what());
									}
								}
							}
						}
					} else {
						TOAST_WARN("AssetManager", "Schema UID {} not found in manifest (asset {})", *schema_uid_str, info.path);
					}
				}
			}
		}
		return schema_handle;
	};

	if (info.type == "node") {
		if (load_mode == SaveMode::editor) {
			std::istringstream stream(std::string(reinterpret_cast<const char*>(raw_data->data()), raw_data->size()));
			asset = std::make_unique<Prefab>(stream);
		} else {
			asset = std::make_unique<Prefab>(std::span<const uint8_t>(*raw_data));
		}
	}

	// raw binary
	else if (AssetRegistry::hasRaw(info.type)) {
		try {
			asset = AssetRegistry::createRaw(info.type, std::move(*raw_data));
		} catch (const std::exception& err) {
			TOAST_ERROR("AssetManager", "Failed to create asset {}: {}", info.path, err.what());
			return nullptr;
		}
	}

	// plain TOML loaders
	else if (AssetRegistry::hasToml(info.type)) {
		try {
			std::string_view toml_str(reinterpret_cast<const char*>(raw_data->data()), raw_data->size());
			asset = AssetRegistry::createToml(info.type, toml::parse(toml_str));
		} catch (const toml::parse_error& err) {
			TOAST_ERROR("AssetManager", "Failed to parse TOML asset {}: {}", info.path, err.description());
			return nullptr;
		} catch (const std::exception& err) {
			TOAST_ERROR("AssetManager", "Failed to load TOML asset {}: {}", info.path, err.what());
			return nullptr;
		}
	}

	// TOML + Schema loaders
	else if (AssetRegistry::hasSchemaToml(info.type)) {
		try {
			std::string_view toml_str(reinterpret_cast<const char*>(raw_data->data()), raw_data->size());
			auto table = toml::parse(toml_str);
			auto schema_handle = resolve_schema(table);
			asset = AssetRegistry::createSchemaToml(info.type, std::move(table), std::move(schema_handle));
		} catch (const toml::parse_error& err) {
			TOAST_ERROR("AssetManager", "Failed to parse TOML asset {}: {}", info.path, err.description());
			return nullptr;
		} catch (const std::exception& err) {
			TOAST_ERROR("AssetManager", "Failed to load TOML+Schema asset {}: {}", info.path, err.what());
			return nullptr;
		}
	}

	else {
		TOAST_ERROR("AssetManager", "Unknown asset type '{}' for asset {}", info.type, info.path);
		return nullptr;
	}

	if (!asset) {
		TOAST_ERROR("AssetManager", "Failed to create asset of type '{}' for {}", info.type, info.path);
		return nullptr;
	}

	Asset* ptr = asset.get();
	cache[id] = std::move(asset);

	TOAST_TRACE("AssetManager", "Loaded asset: {} ({})", info.path, info.type);
	return ptr;
}

auto AssetManager::load(std::string_view uri) -> Asset* {
	std::optional<toast::UID> uid;
	{
		std::lock_guard lock(mutex);
		uid = resolveURI(uri);
	}
	if (!uid) {
		TOAST_ERROR("AssetManager", "Could not resolve URI to UID: {}", uri);
		return nullptr;
	}
	return load(*uid);
}

auto AssetManager::save(toast::UID uid) -> bool {
	std::lock_guard lock(mutex);

	auto cache_it = cache.find(uid.data());
	if (cache_it == cache.end()) {
		TOAST_ERROR("AssetManager", "Cannot save asset {}: not in cache", uid);
		return false;
	}

	auto* saveable = dynamic_cast<ISaveable*>(cache_it->second.get());
	TOAST_ASSERT(saveable != nullptr, "AssetManager", "Attempted to save non-saveable asset {}", uid);
	if (!saveable) {
		return false;
	}

	auto manifest_it = manifest.find(uid.data());
	if (manifest_it == manifest.end()) {
		TOAST_ERROR("AssetManager", "Cannot save asset {}: not in manifest", uid);
		return false;
	}

	auto real_path = resolveVirtualPath(manifest_it->second.path);
	if (!real_path) {
		TOAST_ERROR("AssetManager", "Cannot save asset {}: could not resolve path {}", uid, manifest_it->second.path);
		return false;
	}

	auto bytes = saveable->serialize(SaveMode::editor);
	if (!saveFile(*real_path, bytes)) {
		return false;
	}

	TOAST_INFO("AssetManager", "Saved asset: {} ({})", manifest_it->second.path, manifest_it->second.type);
	return true;
}

auto AssetManager::save(std::string_view uri) -> bool {
	std::optional<toast::UID> uid;
	{
		std::lock_guard lock(mutex);
		uid = resolveURI(uri);
	}
	if (!uid) {
		TOAST_ERROR("AssetManager", "Could not resolve URI to UID: {}", uri);
		return false;
	}
	return save(*uid);
}

auto AssetManager::saveBytes(std::string_view uri, const std::vector<uint8_t>& data) -> bool {
	std::lock_guard lock(mutex);
	auto real_path = resolveVirtualPath(uri);
	if (!real_path) {
		TOAST_ERROR("AssetManager", "Cannot save bytes: could not resolve path {}", uri);
		return false;
	}
	return saveFile(*real_path, data);
}

auto AssetManager::loadBytes(std::string_view uri) -> std::optional<std::vector<uint8_t>> {
	std::lock_guard lock(mutex);
	auto real_path = resolveVirtualPath(uri);
	if (!real_path) {
		TOAST_ERROR("AssetManager", "Cannot load bytes: could not resolve path {}", uri);
		return std::nullopt;
	}
	TOAST_WARN("AssetManager", "Loading {} directly from bytes", uri);
	return openFile(*real_path);
}

auto AssetManager::tryLoadBytes(std::string_view uri) -> std::optional<std::vector<uint8_t>> {
	std::lock_guard lock(mutex);

	const auto sep = uri.find("://");
	if (sep == std::string_view::npos) {
		return std::nullopt;
	}

	if (mounts.contains(std::string(uri.substr(0, sep)))) {
		return readVirtualPath(uri);
	}

	const auto real_path = resolveVirtualPath(uri);
	std::error_code ec;
	if (!real_path || !std::filesystem::exists(*real_path, ec)) {
		return std::nullopt;
	}
	return openFile(*real_path);
}

void AssetManager::reloadManifest() {
	ZoneScoped;
	clearUnusedAssets();

	std::lock_guard lock(mutex);
	manifest.clear();

	// Build the list of per-database manifest paths.
	// Editor mode: manifests live under cache://
	// Game mode: manifests are packed inside each db:// pack
	std::vector<std::string> manifest_uris;
	if (toast::ProjectSettings* ps = toast::ProjectSettings::get()) {
		for (const auto& db : toast::ProjectSettings::databases()) {
			if (load_mode == SaveMode::game) {
				manifest_uris.push_back(db + "://" + db + ".json");
			} else {
				manifest_uris.push_back("cache://" + db + ".json");
			}
		}
	} else {
		manifest_uris.emplace_back("cache://database.json");
	}
	if (load_mode == SaveMode::game) {
		manifest_uris.emplace_back("core://core.json");
	} else {
		manifest_uris.emplace_back("cache://core.json");
	}

	// Load and merge every manifest file
	auto load_manifest = [&](const std::string& uri) {
		auto raw_json = readVirtualPath(uri);
		if (!raw_json) {
			TOAST_WARN("AssetManager", "Manifest not found: {}", uri);
			return;
		}

		try {
			auto json = nlohmann::json::parse(raw_json->begin(), raw_json->end());

			// an entry is a "uid": "virtual path" pair
			auto load_collection = [&](std::string_view type) {
				auto it = json.find(type);
				if (it == json.end() || !it->is_object()) {
					return;
				}
				for (const auto& [key, value] : it->items()) {
					manifest[toast::UID::fromString(key)] = {value.get<std::string>(), std::string(type)};
				}
			};

			load_collection("mesh");
			load_collection("material");
			load_collection("texture");
			load_collection("schema");
			load_collection("data");
			load_collection("node");
			load_collection("curve");
			load_collection("audio_bank");
			load_collection("audio_bus");
			load_collection("audio_event");
			load_collection("audio_port");
			load_collection("audio_snapshot");
			load_collection("audio_strings");
			load_collection("audio_vca");
			load_collection("haptic");
			load_collection("input_action");
			load_collection("input_layout");
			load_collection("input_settings");
			load_collection("script");
		} catch (const std::exception& e) { TOAST_ERROR("AssetManager", "Failed to parse manifest {}: {}", uri, e.what()); }
	};

	for (const auto& uri : manifest_uris) {
		load_manifest(uri);
	}

	TOAST_INFO("AssetManager", "Manifest reloaded: {} assets tracked", manifest.size());
}

void AssetManager::clearUnusedAssets() {
	ZoneScoped;

	std::lock_guard lock(mutex);
	size_t initial_count = cache.size();
	std::erase_if(cache, [](const auto& item) { return item.second->refCount() == 0; });
	size_t cleared = initial_count - cache.size();
	if (cleared > 0) {
		TOAST_INFO("AssetManager", "Cleared {} unused assets from cache", cleared);
	}
}

void AssetManager::setPaths(Paths&& paths) {
	roots["project"] = std::move(paths.project);
	roots["artwork"] = std::move(paths.artworks);
	roots["cache"] = std::move(paths.cache);
	roots["saved"] = std::move(paths.saved);
	roots["core"] = std::move(paths.core);
}

void AssetManager::registerDatabase(std::string_view name, std::filesystem::path root) {
	roots[std::string(name)] = std::move(root);
}

void AssetManager::clearDatabases() {
	// Keep only the five fixed special schemes; erase everything else
	std::erase_if(roots, [](const auto& kv) {
		const auto& k = kv.first;
		return k != "project" && k != "artwork" && k != "cache" && k != "saved" && k != "core";
	});
}

auto AssetManager::projectRoot() -> const std::filesystem::path& {
	static const std::filesystem::path empty;
	auto it = roots.find("project");
	return it != roots.end() ? it->second : empty;
}

auto AssetManager::resolveVirtualPath(std::string_view virtual_path) -> std::optional<std::filesystem::path> {
	const auto sep = virtual_path.find("://");
	if (sep == std::string_view::npos) {
		return std::nullopt;
	}
	const auto scheme = std::string(virtual_path.substr(0, sep));
	const auto it = roots.find(scheme);
	if (it == roots.end()) {
		return std::nullopt;
	}
	return it->second / std::filesystem::path(virtual_path.substr(sep + 3));
}

auto AssetManager::readVirtualPath(std::string_view virtual_path) -> std::optional<std::vector<uint8_t>> {
	const auto sep = virtual_path.find("://");
	if (sep == std::string_view::npos) {
		TOAST_ERROR("AssetManager", "readVirtualPath: malformed URI '{}'", virtual_path);
		return std::nullopt;
	}

	const std::string scheme(virtual_path.substr(0, sep));
	const std::string rel(virtual_path.substr(sep + 3));

	// Consult mounted pack first
	if (const auto mount_it = mounts.find(scheme); mount_it != mounts.end()) {
		auto data = mount_it->second->read(rel);
		if (data) {
			return data;
		}
		// Not found in pack
		TOAST_WARN("AssetManager", "Pack mount '{}://' does not contain '{}', falling back to filesystem", scheme, rel);
	}

	// Filesystem fallback
	auto real_path = resolveVirtualPath(virtual_path);
	if (!real_path) {
		TOAST_ERROR("AssetManager", "Could not resolve virtual path: {}", virtual_path);
		return std::nullopt;
	}
	return openFile(*real_path);
}

auto AssetManager::openFile(const std::filesystem::path& path) -> std::optional<std::vector<uint8_t>> {
	std::ifstream ifs(path, std::ios::binary | std::ios::ate);
	if (!ifs.is_open()) {
		TOAST_ERROR("AssetManager", "Could not open file: {}", path.string());
		return std::nullopt;
	}

	auto size = ifs.tellg();
	ifs.seekg(0, std::ios::beg);

	std::vector<uint8_t> data(size);
	if (!ifs.read(reinterpret_cast<char*>(data.data()), size)) {
		TOAST_ERROR("AssetManager", "Failed to read file: {}", path.string());
		return std::nullopt;
	}

	return data;
}

auto AssetManager::saveFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) -> bool {
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
	if (not ofs.is_open()) {
		TOAST_ERROR("AssetManager", "Could not create or open file {}", path.string());
		return false;
	}

	ofs.write(reinterpret_cast<const char*>(data.data()), data.size());

	if (ofs.fail()) {
		TOAST_ERROR("AssetManager", "Could not write to file {}", path.string());
		return false;
	}

	ofs.close();
	return true;
}

auto AssetManager::resolveURI(std::string_view uri) -> std::optional<toast::UID> {
	for (const auto& [uid_val, info] : instance->manifest) {
		if (info.path == uri) {
			return toast::UID(uid_val);
		}
	}
	return std::nullopt;
}

auto AssetManager::getURI(toast::UID uid) -> std::string {
	if (not instance) {
		return {};
	}
	auto it = instance->manifest.find(uid.data());
	if (it != instance->manifest.end()) {
		return it->second.path;
	}
	return {};
}

auto AssetManager::search(std::string_view query) -> std::vector<AssetHandle<Asset>> {
	std::vector<toast::UID> matches;
	{
		std::lock_guard lock(mutex);
		for (const auto& [uid_val, info] : manifest) {
			if (info.path.contains(query)) {
				matches.emplace_back(uid_val);
			}
		}
	}

	std::vector<AssetHandle<Asset>> results;
	results.reserve(matches.size());
	for (const auto& uid : matches) {
		if (auto* asset = load(uid)) {
			auto uri = getURI(uid);
			results.emplace_back(asset, uid, uri);
		}
	}
	return results;
}

auto AssetManager::getCachePath() const -> const std::filesystem::path& {
	static const std::filesystem::path empty;
	auto it = roots.find("cache");
	return it != roots.end() ? it->second : empty;
}

auto AssetManager::listByType(std::string_view type) -> std::vector<toast::UID> {
	std::vector<toast::UID> result;
	std::lock_guard lock(mutex);
	for (const auto& [uid_int, info] : manifest) {
		if (info.type == type) {
			result.emplace_back(uid_int);
		}
	}
	return result;
}

auto AssetManager::typeOf(toast::UID uid) -> std::string {
	auto& manager = get();
	std::lock_guard lock(manager.mutex);
	auto it = manager.manifest.find(uid.data());
	return it != manager.manifest.end() ? it->second.type : std::string {};
}

void AssetManager::pollModifiedAssets() {
	ZoneScoped;

	struct ChangedAsset {
		toast::UID uid;
		std::string type;
	};

	std::vector<ChangedAsset> changed;
	{
		std::lock_guard lock(mutex);
		for (auto& [id, asset] : cache) {
			auto manifest_it = manifest.find(id);
			if (manifest_it == manifest.end()) {
				continue;
			}
			const std::string& type = manifest_it->second.type;
			if (type != "script" && type != "shader" && type != "material" && type != "material_instance") {
				continue;
			}
			auto real_path = resolveVirtualPath(manifest_it->second.path);
			if (!real_path) {
				continue;
			}

			std::error_code ec;
			const auto mtime = std::filesystem::last_write_time(*real_path, ec);
			if (ec) {
				continue;
			}
			auto [it, first_seen] = asset_mtimes.try_emplace(id, mtime);
			if (first_seen || it->second == mtime) {
				continue;    // unchanged
			}
			it->second = mtime;

			auto raw = readVirtualPath(manifest_it->second.path);
			if (!raw) {
				continue;
			}

			if (type == "script") {
				static_cast<Script*>(asset.get())->setData(std::move(*raw));
			} else if (type == "shader") {
				static_cast<Shader*>(asset.get())->setSource(std::move(*raw));
			} else {
				// Materials re-parse their TOML in place so existing handles stay valid
				try {
					const std::string_view toml_str(reinterpret_cast<const char*>(raw->data()), raw->size());
					static_cast<Data*>(asset.get())->reload(toml::parse(toml_str));
				} catch (const toml::parse_error& err) {
					TOAST_ERROR("AssetManager", "Hot reload parse error for {}: {}", manifest_it->second.path, err.description());
					continue;
				}
			}
			changed.push_back(ChangedAsset {.uid = toast::UID(id), .type = type});
		}
	}

	for (const auto& [uid, type] : changed) {
		TOAST_INFO("AssetManager", "Asset changed on disk, reloading: {} ({})", getURI(uid), type);
		if (type == "script") {
			event::send<event::ScriptAssetReloaded>(uid);
		} else if (type == "shader") {
			event::send<event::ShaderAssetReloaded>(uid);
		} else {
			event::send<event::MaterialAssetReloaded>(uid);
		}
	}
}

// Public API Implementations
auto load(toast::UID uid) -> AssetHandleBase {
	return {AssetManager::get().load(uid), uid, AssetManager::getURI(uid)};
}

auto load(std::string_view uri) -> AssetHandleBase {
	auto uid = AssetManager::resolveURI(uri);
	if (not uid.has_value()) {
		TOAST_ERROR("AssetManager", "Could not resolve URI to UID: {}", uri);
		return AssetHandleBase(nullptr);
	}
	return load(*uid);
}

auto resolveURI(std::string_view uri) -> std::optional<toast::UID> {
	return AssetManager::resolveURI(uri);
}

auto save(toast::UID uid) -> bool {
	return AssetManager::get().save(uid);
}

auto listByType(std::string_view type) -> std::vector<toast::UID> {
	return AssetManager::get().listByType(type);
}

auto typeOf(toast::UID uid) -> std::string {
	return AssetManager::typeOf(uid);
}
}
