/**
 * @file ResourceManager.hpp
 * @author Dario
 * @date 17/09/25
 * @brief Resource loading and caching system.
 *
 * This file provides the ResourceManager class which handles loading,
 * caching, and managing all game resources (textures, meshes, shaders, etc.).
 */

#pragma once
#include "ResourceSlot.hpp"
#include "Toast/Renderer/Shader.hpp"

#include <Toast/Log.hpp>
#include <string>
#include <thread>
#include <vector>

namespace resource {
/**
 * @class ResourceManager
 * @brief Singleton manager for loading and caching game resources.
 *
 * The ResourceManager provides centralized resource loading with automatic
 * caching to prevent duplicate loads. It supports loading from the filesystem
 * or from a packed (.pkg) file for distribution.
 *
 * @par Features:
 * - Automatic caching of loaded resources
 * - Background loading with main-thread GPU upload
 * - Support for packed resource files (.pkg)
 * - Automatic resource purging when unused
 *
 * @par Supported Resource Types:
 * - Texture: Images (.png, .jpg)
 * - Mesh: 3D models (.obj)
 * - Shader: GPU shader programs
 * - Material: Shader + texture combinations
 *
 * @par Usage Example:
 * @code
 * auto* rm = ResourceManager::GetInstance();
 *
 * // Load a texture (cached automatically)
 * auto texture = rm->LoadResource<Texture>("textures/player.png");
 *
 * // Load same texture again - returns cached version
 * auto sameTexture = rm->LoadResource<Texture>("textures/player.png");
 *
 * // Load a shader
 * auto shader = rm->LoadResource<Shader>("shaders/standard.shader");
 *
 * // Read raw file data
 * auto contents = resource::Open("data/config.json");
 * @endcode
 *
 * @note GPU resources are automatically uploaded on the main thread.
 * @warning ResourceManager must be created before loading any resources.
 *
 * @see IResource, Texture, Mesh, Shader
 */
class ResourceManager {
public:
	/**
	 * @brief Constructs the ResourceManager.
	 * @param pkg If true, reads from game.pkg instead of filesystem.
	 */
	ResourceManager(bool pkg = false);

	~ResourceManager();

	/**
	 * @brief Gets the singleton instance.
	 * @return Pointer to the ResourceManager.
	 */
	[[nodiscard]]
	static ResourceManager* GetInstance();

	/**
	 * @brief Uploads pending GPU resources on the main thread.
	 *
	 * Call this once per frame from the main thread to upload
	 * textures and other GPU resources that were loaded in the background.
	 */
	void LoadResourcesMainThread();

	/**
	 * @brief Purges unused resources from the cache.
	 *
	 * Removes resources with no external references. Called automatically
	 * by the engine every 120 seconds.
	 */
	void PurgeResources();

	/**
	 * @brief Loads a resource of the specified type.
	 *
	 * If the resource is already cached, returns the cached version.
	 * Otherwise, loads from disk (or pkg) and caches it.
	 *
	 * @tparam R Resource type (must inherit from IResource).
	 * @tparam Args Additional constructor argument types.
	 * @param path Path to the resource file (relative to assets/).
	 * @param args Additional arguments passed to the resource constructor.
	 * @return Shared pointer to the loaded resource.
	 *
	 * @par Example:
	 * @code
	 * auto texture = rm->LoadResource<Texture>("textures/player.png");
	 * auto mesh = rm->LoadResource<Mesh>("models/cube.obj");
	 * @endcode
	 */
	template<typename R, typename... Args>
	std::shared_ptr<R> LoadResource(const std::string& path, Args&&... args);

	/**
	 * @brief Opens a file and returns its contents as a string stream.
	 * @param path File path (relative to assets/).
	 * @param data_out Output string stream with file contents.
	 * @return true if file was opened successfully.
	 */
	bool OpenFile(const std::string& path, std::istringstream& data_out) const;

	/**
	 * @brief Opens a file and returns its contents as bytes.
	 * @param path File path (relative to assets/).
	 * @param data Output vector with file bytes.
	 * @return true if file was opened successfully.
	 */
	bool OpenFile(const std::string& path, std::vector<uint8_t>& data) const;

	/**
	 * @brief Saves content to a file on disk.
	 * @param path File path (relative to assets/).
	 * @param content Content to write.
	 * @return true if file was saved successfully.
	 */
	static bool SaveFile(const std::string& path, const std::string& content);

	/**
	 * @brief Creates a resource slot entry for the editor.
	 * @param path Path to the resource file.
	 * @return ResourceSlot entry for editor display.
	 */
	static editor::ResourceSlot::Entry CreateResourceSlotEntry(const std::filesystem::path& path);

	/**
	 * @brief Gets the cached resources map.
	 * @return Reference to the cache map.
	 */
	std::unordered_map<std::string, std::shared_ptr<IResource>>& GetCachedResources() {
		return m_cachedResources;
	}

private:
	
	/**
	 * @brief Converts backslashes to forward slashes.
	 * @param s Input path string.
	 * @return Normalized path string.
	 */
	[[nodiscard]]
	inline std::string ToForwardSlashes(const std::string& s) const {
		std::string result = s;
		std::ranges::replace(result, '\\', '/');
		return result;
	}

	/// @brief Mutex protecting the resource cache.
	std::mutex m_mtx;

	/// @brief Mutex protecting the upload queue.
	std::mutex m_uploadMtx;

	/// @brief ID of the main thread for GPU upload checks.
	std::thread::id m_mainThreadId;

	/// @brief Singleton instance pointer.
	static ResourceManager* m_instance;

	/// @brief Queue of resources pending GPU upload.
	std::vector<std::weak_ptr<IResource>> m_uploadResources;

	/// @brief Cache of loaded resources.
	std::unordered_map<std::string, std::shared_ptr<IResource>> m_cachedResources;

	/// @brief Whether to read from .pkg file.
	bool m_pkg = false;
};

inline auto Open(std::string const& path) -> std::optional<std::string> {
	std::istringstream fileStream;
	if (!ResourceManager::GetInstance()->OpenFile(path, fileStream)) {
		return std::nullopt;
	}
	
	return fileStream.str();
}

inline auto Open(std::string const& path, std::istringstream& data) {
	return ResourceManager::GetInstance()->OpenFile(path, data);
}

inline auto Open(std::string const& path, std::vector<uint8_t>& data_out) {
	return ResourceManager::GetInstance()->OpenFile(path, data_out);
}

template<typename R, typename... Args>
inline auto LoadResource(const std::string& path, Args&&... args) {
	return ResourceManager::GetInstance()->LoadResource<R>(path, std::forward<Args>(args)...);
}

inline auto SaveFile(const std::string& path, const std::string& content) -> bool {
	return ResourceManager::SaveFile(path, content);
}

inline auto PurgeResources() {
	ResourceManager::GetInstance()->PurgeResources();
}

template<typename R, typename... Args>
std::shared_ptr<R> ResourceManager::LoadResource(const std::string& path, Args&&... args) {
	static_assert(std::is_base_of_v<IResource, R>, "Must be IResource type");

	// Normalize path to use forward slashes
	std::string formattedPath = ToForwardSlashes(path);

	TOAST_INFO("Loading resource: {0}", formattedPath);

	// Fast path: try to return cached
	{
		std::lock_guard lock(m_mtx);
		auto it = m_cachedResources.find(formattedPath);
		if (it != m_cachedResources.end()) {
			// lock the base weak_ptr to get a shared_ptr<IResource>
			if (auto baseSp = it->second) {
				// try to cast to derived R
				if (auto derivedSp = std::dynamic_pointer_cast<R>(baseSp)) {
					return derivedSp;
				}
				// cached resource exists but not of requested derived type
				return std::shared_ptr<R> {};
			}
			// else: expired weak_ptr -> fallthrough to create new
		}
	}

	// Create the object first (owning pointer) - perfect-forward extra args (optional)
	auto res = std::make_shared<R>(formattedPath, std::forward<Args>(args)...);

	// Insert weak_ptr into cache BEFORE performing the expensive load so other threads see it.
	// Important: don't std::move(res) here — we still need the shared_ptr locally to call Load().
	{
		std::lock_guard lock(m_mtx);
		m_cachedResources[formattedPath] = res;    // constructs weak_ptr<IResource> from shared_ptr<R>
	}

	// Now load without holding the resource map mutex.
	res->Load();

	// If the resource needs GPU upload, enqueue an upload job that captures the shared_ptr.
	if (res->IsGPU()) {
		if (m_mainThreadId == std::this_thread::get_id()) {
			// If we're already on the main thread, load immediately
			res->LoadMainThread();
		} else {
			std::lock_guard lock(m_uploadMtx);
			m_uploadResources.emplace_back(res);
		}
	}

	// Return the shared_ptr<R> for the caller
	return res;
}
}
