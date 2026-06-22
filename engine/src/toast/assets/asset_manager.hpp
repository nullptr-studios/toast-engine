/**
 * @file asset_manager.hpp
 * @author Xein
 * @date 06 Jun 2026
 *
 * @brief Maps five URI prefixes to filesystem roots and manages asset lifetime
 *
 * Supported schemes: assets://, artwork://, cache://, core://, saved://
 * Roots are configured via setPaths() before any load call
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

/**
 * @brief Maps five URI schemes to filesystem roots and manages asset lifetime
 *
 * Supported schemes and their intended roots:
 *   - assets://   content addressed by UID (game data, levels, characters)
 *   - artwork://  raw art source files (textures, models before baking)
 *   - cache://    generated or baked files that can be rebuilt
 *   - core://     engine built-in assets (default shaders, primitives)
 *   - saved://    user save data and preferences
 *
 * Roots are configured once via setPaths() before any load call. Assets stay resident
 * in the cache until clearUnusedAssets() is called and their ref count is zero.
 *
 * @see Paths, AssetHandle, setPaths()
 */
class AssetManager {
public:
	AssetManager();
	~AssetManager() = default;

	static auto get() noexcept -> AssetManager&;

	/**
	 * @brief Loads an asset by UID, returning the cached copy if already resident
	 * @param uid The asset's manifest UID
	 * @return Raw pointer owned by the manager; do not delete; null if the asset was not found or failed to parse
	 * @note The returned pointer stays valid until clearUnusedAssets() is called and the ref count reaches zero
	 */
	auto load(toast::UID uid) -> Asset*;

	/**
	 * @brief Loads an asset by virtual URI, returning the cached copy if already resident
	 * @param uri Virtual path, e.g. "assets://characters/knight.node"
	 * @return Raw pointer owned by the manager; null if the URI could not be resolved or the asset failed to parse
	 * @note Resolves the URI to a UID via the manifest, then delegates to load(UID)
	 */
	auto load(std::string_view uri) -> Asset*;

	/**
	 * @brief Serializes the cached asset at the given UID back to its backing file
	 * @param uid The asset's manifest UID
	 * @return false if the asset is not currently cached or the write fails
	 */
	auto save(toast::UID uid) -> bool;

	/**
	 * @brief Serializes the cached asset at the given URI back to its backing file
	 * @param uri Virtual path of the asset to save
	 * @return false if the URI cannot be resolved, the asset is not cached, or the write fails
	 */
	auto save(std::string_view uri) -> bool;

	/**
	 * @brief Writes raw bytes to the path resolved by a virtual URI, bypassing the manifest
	 * @param uri Virtual path, e.g. "cache://generated/mesh.bin"
	 * @param data Bytes to write
	 * @return false on write failure
	 * @note Useful for newly created assets that are not yet in the manifest; does not update the manifest
	 */
	auto saveBytes(std::string_view uri, const std::vector<uint8_t>& data) -> bool;

	/**
	 * @brief Re-reads the project manifest from disk
	 * @note Call after importing or creating a new asset; does not evict the asset cache
	 */
	void reloadManifest();

	/**
	 * @brief Evicts all assets whose ref count is zero from the cache
	 * @note Assets still referenced by an AssetHandle will not be evicted; safe to call periodically
	 */
	void clearUnusedAssets();

	/**
	 * @brief Configures the five URI roots; must be called once before any load() call
	 * @param paths Filesystem paths for each URI scheme
	 * @warning Not thread-safe after initialization; call this before creating the AssetManager or spawning load threads
	 */
	static void setPaths(Paths&& paths);

	/**
	 * @brief Maps a virtual URI to its manifest UID
	 * @param uri A string of the form scheme://relative/path
	 * @return The UID from the manifest, or nullopt if the scheme is unknown or the path has no manifest entry
	 */
	static auto resolveURI(std::string_view uri) -> std::optional<toast::UID>;

	auto getCachePath() const -> const std::filesystem::path&;

private:
	static inline AssetManager* instance = nullptr;

	// FIXME: hardcoded; needs to become runtime-selectable once the game player is built
	static constexpr SaveMode load_mode = SaveMode::editor;

	event::Listener listener;
	std::mutex mutex;
	std::unordered_map<uint64_t, AssetInfo> manifest;    ///< UID → path+type; populated from the project manifest on construction
	std::unordered_map<uint64_t, std::unique_ptr<Asset>> cache;    ///< assets stay resident until clearUnusedAssets() is called

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
