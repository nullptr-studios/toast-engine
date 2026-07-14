/**
 * @file asset_manager.hpp
 * @author Xein
 * @date 06 Jun 2026
 *
 * @brief Maps URI schemes to filesystem roots and manages asset lifetime
 *
 * Fixed special schemes: project://, artwork://, cache://, core://, saved://
 * Content database schemes: any name registered via registerDatabase() or
 * derived from ProjectSettings::databases() (e.g. assets://, dlc://, ...)
 *
 * Fixed roots are configured via setPaths() before any load call.
 * Content databases are added via registerDatabase() after ProjectSettings loads.
 */

#pragma once

#include "assets.hpp"
#include "pack.hpp"
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
	std::filesystem::path project;    ///< project root folder; special databases are derived from here
	std::filesystem::path artworks;
	std::filesystem::path cache;
	std::filesystem::path saved;
	std::filesystem::path core;
};

/**
 * @brief Maps URI schemes to filesystem roots and manages asset lifetime
 *
 * Fixed special schemes (always registered via setPaths):
 *   - project://  the project root folder
 *   - artwork://  raw art source files (textures, models before baking)
 *   - cache://    generated or baked files that can be rebuilt
 *   - core://     engine built-in assets (default shaders, primitives)
 *   - saved://    user save data and preferences
 *
 * Content database schemes (registered via registerDatabase after ProjectSettings loads):
 *   - assets://   default content database (always present for a new project)
 *   - <name>://   additional databases listed in the project's databases array
 *
 * Roots are configured via setPaths() then registerDatabase() before any load call.
 * Assets stay resident in the cache until clearUnusedAssets() is called and their
 * ref count is zero.
 *
 * @see Paths, AssetHandle, setPaths(), registerDatabase()
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
	 * @brief Reads raw bytes from the URI, bypassing the manifest
	 *
	 * You should 99% of the time not use this
	 */
	auto loadBytes(std::string_view uri) -> std::optional<std::vector<uint8_t>>;

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
	 * @brief Configures the five fixed special URI roots; must be called before Engine::init()
	 * @param paths Filesystem paths for each fixed scheme (project, artwork, cache, core, saved)
	 * @warning Not thread-safe after initialization; call this before creating the AssetManager or spawning load threads
	 */
	static void setPaths(Paths&& paths);

	/**
	 * @brief Registers a content database scheme
	 * @note Call after setPaths and before AssetManager construction (or before reloadManifest)
	 */
	static void registerDatabase(std::string_view name, std::filesystem::path root);

	/**
	 * @brief Removes all content database schemes (non-fixed), leaving only special roots intact
	 * @note Used by reloadSettings to reset before re-registering databases from updated project settings
	 */
	static void clearDatabases();

	/**
	 * @brief Returns the project root path (project:// root)
	 */
	static auto projectRoot() -> const std::filesystem::path&;

	/**
	 * @brief Maps a virtual URI to its manifest UID
	 * @param uri A string of the form scheme://relative/path
	 * @return The UID from the manifest, or nullopt if the scheme is unknown or the path has no manifest entry
	 */
	static auto resolveURI(std::string_view uri) -> std::optional<toast::UID>;

	/**
	 * @returns The path of the given UID
	 *
	 * This is useful for debugging, or printing which file you have
	 */
	static auto getURI(toast::UID uid) -> std::string;

	/**
	 * @brief Searches the manifest for entries whose path contains the given query string
	 * @param query Substring to search for in manifest paths
	 * @return Loaded asset handles for all matching entries; empty vector if none found
	 */
	auto search(std::string_view query) -> std::vector<AssetHandle<Asset>>;

	auto listByType(std::string_view type) -> std::vector<toast::UID>;

	/**
	 * @brief Looks up an asset's type string in the manifest without loading it
	 * @return The manifest type
	 */
	static auto typeOf(toast::UID uid) -> std::string;

	/**
	 * @brief Re-reads any cached Script asset whose file changed on disk (hot reload)
	 */
	void pollModifiedScripts();

	auto getCachePath() const -> const std::filesystem::path&;

	/**
	 * @brief Selects the asset load mode
	 */
	static void setLoadMode(SaveMode mode);

	/**
	 * @return Current load mode
	 */
	static auto getLoadMode() -> SaveMode;

	/**
	 * @brief Mounts a .pak archive at a URI scheme
	 * @param scheme URI scheme without "://"
	 * @param pak_path Absolute path to the .pak file
	 *
	 * After mounting, reads for that scheme go to the pack instead of the filesystem
	 */
	static void mountPack(std::string_view scheme, const std::filesystem::path& pak_path);

private:
	static inline AssetManager* instance = nullptr;

	static inline SaveMode load_mode = SaveMode::editor;

	static inline std::unordered_map<std::string, std::unique_ptr<PackArchive>> mounts;

	event::Listener listener;
	std::mutex mutex;
	std::unordered_map<uint64_t, AssetInfo> manifest;    ///< UID → path+type; populated from the project manifest on construction
	std::unordered_map<uint64_t, std::unique_ptr<Asset>> cache;    ///< assets stay resident until clearUnusedAssets() is called
	std::unordered_map<uint64_t, std::filesystem::file_time_type> script_mtimes;    ///< last seen mtime per cached script

	/// Scheme name (no "://") → filesystem root. Populated by setPaths() + registerDatabase().
	static inline std::unordered_map<std::string, std::filesystem::path> roots;

	auto resolveVirtualPath(std::string_view virtual_path) -> std::optional<std::filesystem::path>;
	auto readVirtualPath(std::string_view virtual_path) -> std::optional<std::vector<uint8_t>>;
	auto openFile(const std::filesystem::path& path) -> std::optional<std::vector<uint8_t>>;
	auto saveFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) -> bool;
};

}
