#include "asset_manager.hpp"

#include "prefab.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <toast/log.hpp>

namespace assets {

AssetManager::AssetManager() {
	instance = this;

	reloadManifest();

	listener.subscribe<event::ReloadAssetsManifest>([this] { reloadManifest(); });
	listener.subscribe<event::ClearUnusedAssets>([this] { clearUnusedAssets(); });
}

auto AssetManager::get() noexcept -> AssetManager& {
	TOAST_ASSERT(instance != nullptr, "AssetManager", "AssetManager instance is not initialized");
	return *instance;
}

auto AssetManager::load(toast::UID uid) -> Asset* {
	uint64_t id = uid.data();

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

	auto raw_data = openFile(*real_path);
	if (!raw_data) {
		return nullptr;
	}

	std::unique_ptr<Asset> asset = nullptr;

	if (info.type == "texture") {
		asset = std::make_unique<Texture>(std::move(*raw_data));
	} else if (info.type == "data") {
		try {
			std::string_view toml_str(reinterpret_cast<const char*>(raw_data->data()), raw_data->size());
			asset = std::make_unique<Data>(toml::parse(toml_str));
		} catch (const toml::parse_error& err) {
			TOAST_ERROR("AssetManager", "Failed to parse TOML asset {}: {}", info.path, err.description());
			return nullptr;
		}
	} else if (info.type == "node") {
		// TODO: At some point we need to handle toast packs
		if constexpr (load_mode == SaveMode::editor) {
			std::istringstream stream(std::string(reinterpret_cast<const char*>(raw_data->data()), raw_data->size()));
			asset = std::make_unique<Prefab>(stream);
		} else {
			asset = std::make_unique<Prefab>(std::span<const uint8_t>(*raw_data));
		}
	} else {
		TOAST_ERROR("AssetManager", "Unknown asset type '{}' for asset {}", info.type, info.path);
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

void AssetManager::reloadManifest() {
	std::lock_guard lock(mutex);
	manifest.clear();

	auto db_path = resolveVirtualPath("cache://database.json");
	if (!db_path || !std::filesystem::exists(*db_path)) {
		TOAST_WARN("AssetManager", "Asset database not found at {}", db_path ? db_path->string() : "invalid path");
		return;
	}

	auto raw_json = openFile(*db_path);
	if (!raw_json) {
		return;
	}

	try {
		auto json = nlohmann::json::parse(raw_json->begin(), raw_json->end());
		if (json.contains("assets") && json["assets"].is_object()) {
			for (const auto& [key, value] : json["assets"].items()) {
				uint64_t uid_val = toast::UID::fromString(key);
				AssetInfo info;
				info.path = value.value("path", "");
				info.type = value.value("type", "");
				manifest[uid_val] = std::move(info);
			}
		}
		TOAST_INFO("AssetManager", "Manifest reloaded: {} assets tracked", manifest.size());
	} catch (const std::exception& e) { TOAST_ERROR("AssetManager", "Failed to parse asset manifest: {}", e.what()); }
}

void AssetManager::clearUnusedAssets() {
	std::lock_guard lock(mutex);
	size_t initial_count = cache.size();
	std::erase_if(cache, [](const auto& item) { return item.second->refCount() == 0; });
	size_t cleared = initial_count - cache.size();
	if (cleared > 0) {
		TOAST_INFO("AssetManager", "Cleared {} unused assets from cache", cleared);
	}
}

void AssetManager::setPaths(Paths&& paths) {
	assets_path = std::move(paths.assets);
	artworks_path = std::move(paths.artworks);
	cache_path = std::move(paths.cache);
	core_path = std::move(paths.core);
	saved_path = std::move(paths.saved);
}

auto AssetManager::resolveVirtualPath(std::string_view virtual_path) -> std::optional<std::filesystem::path> {
	if (virtual_path.starts_with(assets_uri)) {
		return assets_path / virtual_path.substr(assets_uri.size());
	}
	if (virtual_path.starts_with(artwork_uri)) {
		return artworks_path / virtual_path.substr(artwork_uri.size());
	}
	if (virtual_path.starts_with(cache_uri)) {
		return cache_path / virtual_path.substr(cache_uri.size());
	}
	if (virtual_path.starts_with(core_uri)) {
		return core_path / virtual_path.substr(core_uri.size());
	}
	if (virtual_path.starts_with(saved_uri)) {
		return saved_path / virtual_path.substr(saved_uri.size());
	}
	return std::nullopt;
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

// Public API Implementations
auto load(toast::UID uid) -> AssetHandleBase {
	return AssetHandleBase(AssetManager::get().load(uid));
}

auto load(std::string_view uri) -> AssetHandleBase {
	return AssetHandleBase(AssetManager::get().load(uri));
}

auto resolveURI(std::string_view uri) -> std::optional<toast::UID> {
	return AssetManager::resolveURI(uri);
}

auto save(toast::UID uid) -> bool {
	return AssetManager::get().save(uid);
}
}
