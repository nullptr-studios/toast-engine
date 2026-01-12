///@file ResourceManager.hpp
///@author Dario
///@date 17/09/25

#pragma once
#include "ResourceSlot.hpp"
#include "Texture.hpp"
#include "Toast/Log.hpp"
#include "Toast/Renderer/Shader.hpp"

#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace resource {

///@class ResourceManager
///@brief Manager of every resource in the engine
class ResourceManager {
public:
	///@brief enable pkg file reading
	///@param pkg true to enable, false to disable
	ResourceManager(bool pkg = false);
	~ResourceManager();

	[[nodiscard]]
	static ResourceManager* GetInstance();

	///@brief Loads the resources tagged as GPU resources on the main thread
	/// This function should be called once per frame on the main thread
	void LoadResourcesMainThread();

	///@brief Purges unused resources from the cache
	/// gets called every 20 seconds
	void PurgeResources();

	///@brief Loads a resource of type R from the given path
	template<typename R, typename... Args>
	std::shared_ptr<R> LoadResource(const std::string& path, Args&&... args);

	// std::shared_ptr<renderer::Shader> LoadShader(std::string name);

	///@brief opens a file from disk or pkg
	bool OpenFile(const std::string& path, std::istringstream& data_out) const;
	///@brief opens a file from disk or pkg
	bool OpenFile(const std::string& path, std::vector<uint8_t>& data) const;

	///@brief saves a file to disk
	static bool SaveFile(const std::string& path, const std::string& content);

	static editor::ResourceSlot::Entry CreateResourceSlotEntry(const std::filesystem::path& path);

	std::unordered_map<std::string, std::shared_ptr<IResource>>& GetCachedResources() {
		return m_cachedResources;
	}

private:
	std::mutex m_mtx;
	std::mutex m_uploadMtx;

	std::thread::id m_mainThreadId;

	static ResourceManager* m_instance;

	// Resources that need to be uploaded to the GPU on the main thread
	std::vector<std::weak_ptr<IResource>> m_uploadResources;

	// Cached resources
	std::unordered_map<std::string, std::shared_ptr<IResource>> m_cachedResources;

	// Internal file reading
	bool m_pkg = false;
};

[[nodiscard]]
inline auto Open(const std::string& path) -> std::optional<std::string> {
	std::istringstream s;
	if (!ResourceManager::GetInstance()->OpenFile(path, s)) {
		TOAST_WARN("File {} could not be opened", path);
		return std::nullopt;
	}

	std::string str = s.str();
	return str;
}

template<typename R, typename... Args>
std::shared_ptr<R> ResourceManager::LoadResource(const std::string& path, Args&&... args) {
	static_assert(std::is_base_of_v<IResource, R>, "Must be IResource type");

	TOAST_INFO("Loading resource: {0}", path);

	// Fast path: try to return cached
	{
		std::lock_guard lock(m_mtx);
		auto it = m_cachedResources.find(path);
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
	auto res = std::make_shared<R>(path, std::forward<Args>(args)...);

	// Insert weak_ptr into cache BEFORE performing the expensive load so other threads see it.
	// Important: don't std::move(res) here — we still need the shared_ptr locally to call Load().
	{
		std::lock_guard lock(m_mtx);
		m_cachedResources[path] = res;    // constructs weak_ptr<IResource> from shared_ptr<R>
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
