/**
 * @file asset_manager.hpp
 * @author Xein
 * @date 06 Jun 2026
 *
 * @brief Internal asset manager for the Toast engine
 */

#pragma once

#include "assets.hpp"
#include "toast/events/listener.hpp"
#include "types.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace assets {

struct AssetInfo {
	std::string path;
	std::string type;
};

struct Paths {
	std::filesystem::path assets;
	std::filesystem::path artworks;
	std::filesystem::path cache;
	std::filesystem::path saved;
	std::filesystem::path core;
};

class AssetManager {
public:
	AssetManager();
	~AssetManager() = default;

	static auto get() noexcept -> AssetManager&;

	auto load(toast::UID uid) -> Asset*;
	auto load(std::string_view uri) -> Asset*;

	auto save(toast::UID uid) -> bool;
	auto save(std::string_view uri) -> bool;

	/// @brief Writes bytes to the file backing a virtual path, bypassing the manifest
	auto saveBytes(std::string_view uri, const std::vector<uint8_t>& data) -> bool;

	void reloadManifest();
	void clearUnusedAssets();

	static void setPaths(Paths&& paths);
	static auto resolveURI(std::string_view uri) -> std::optional<toast::UID>;

private:
	static inline AssetManager* instance = nullptr;

	/// TODO: this is hardcoded
	static constexpr SaveMode load_mode = SaveMode::editor;

	event::Listener listener;
	std::mutex mutex;
	std::unordered_map<uint64_t, AssetInfo> manifest;
	std::unordered_map<uint64_t, std::unique_ptr<Asset>> cache;

	static inline std::filesystem::path assets_path;
	static inline std::filesystem::path artworks_path;
	static inline std::filesystem::path cache_path;
	static inline std::filesystem::path core_path;
	static inline std::filesystem::path saved_path;

	auto resolveVirtualPath(std::string_view virtual_path) -> std::optional<std::filesystem::path>;
	auto openFile(const std::filesystem::path& path) -> std::optional<std::vector<uint8_t>>;
	auto saveFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) -> bool;

	static constexpr std::string_view assets_uri = "assets://";
	static constexpr std::string_view artwork_uri = "artwork://";
	static constexpr std::string_view cache_uri = "cache://";
	static constexpr std::string_view core_uri = "core://";
	static constexpr std::string_view saved_uri = "saved://";
};

}
