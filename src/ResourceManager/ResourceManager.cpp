/// @file ResourceManager.cpp
/// @author dario
/// @date 18/09/2025.

#include "PackLoader.hpp"

#include <Toast/Log.hpp>
#include <Toast/Profiler.hpp>
#include <Toast/Resources/ResourceManager.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

//@TODO: Improve error handling and log

resource::ResourceManager* resource::ResourceManager::m_instance = nullptr;

namespace resource {
PackFile g_packFile;

std::shared_ptr<Texture> g_fileIcon;
std::shared_ptr<Texture> g_jsonIcon;
std::shared_ptr<Texture> g_objIcon;

//@TODO: Instead of passing a bool, detect if a .pkg is in the root folder
ResourceManager::ResourceManager(bool pkg) : m_pkg(pkg) {
	if (m_instance == nullptr) {
		m_instance = this;
	}

	m_mainThreadId = std::this_thread::get_id();

	// If pkg is true, open the game.pkg file
	if (m_pkg) {
		//@TODO: Make the .PKG path configurable?
		TOAST_INFO("ResourceManager: Opening resource pack game.pkg");
		if (!g_packFile.Open("game.pkg")) {
			throw ToastException("ResourceManager: Failed to open game.pkg");
		}
	}
}

ResourceManager::~ResourceManager() {
	if (m_pkg) {
		g_packFile.Close();
	}
}

ResourceManager* ResourceManager::GetInstance() {
	return m_instance;
}

void ResourceManager::LoadResourcesMainThread() {
	if (m_uploadResources.empty()) {
		return;
	}

	PROFILE_ZONE;
	// safe swap under lock to drain the queue
	std::vector<std::weak_ptr<IResource>> local;
	local.reserve(m_uploadResources.size());
	{
		std::lock_guard<std::mutex> lg(m_uploadMtx);
		local.swap(m_uploadResources);
	}

	// Process drained list without holding the lock
	for (auto& w : local) {
		if (auto s = w.lock()) {    // lock once, check and use
			s->LoadMainThread();
		}
	}
}

void ResourceManager::PurgeResources() {
	PROFILE_ZONE;
	std::vector<std::shared_ptr<IResource>> toDestroy;
	{
		std::lock_guard lk(m_mtx);
		for (auto it = m_cachedResources.begin(); it != m_cachedResources.end();) {
			if (it->second.use_count() == 1) {
				// move owning shared_ptr out so we can destroy it outside the lock
				toDestroy.push_back(std::move(it->second));
				it = m_cachedResources.erase(it);
			} else {
				++it;
			}
		}
	}

	// Now allow resources to be destroyed
	for (auto& r : toDestroy) {
		r.reset();
	}
}

bool ResourceManager::OpenFile(const std::string& path, std::istringstream& data_out) const {
	std::vector<uint8_t> d {};
	if (!OpenFile(path, d)) {
		return false;
	}

	data_out.str(std::string(reinterpret_cast<char*>(d.data()), d.size()));
	return true;
}

bool ResourceManager::OpenFile(const std::string& path, std::vector<uint8_t>& data) const {
	PROFILE_ZONE;
	// if using pkg, read from pack file
	if (m_pkg) {
		return g_packFile.ReadFile(path, data);
	}

	std::string p;
	// else read from filesystem
	if (path.find("assets/") == std::string::npos) {
		p = "assets/" + path;
	} else {
		p = path;
	}

	std::ifstream ifs(p, std::ios::binary);
	if (!ifs) {
		return false;
	}
	ifs.seekg(0, std::ios::end);
	size_t size = ifs.tellg();
	ifs.seekg(0, std::ios::beg);
	data.resize(size);
	ifs.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
	return true;
}

bool ResourceManager::SaveFile(const std::string& path, const std::string& content) {
	TOAST_INFO("Saving File {}", path);
	std::string pat {};
	if (path.find("assets/") == std::string::npos) {
		pat = "assets/" + path;
	} else {
		pat = path;
	}

	namespace fs = std::filesystem;

	// Ensure parent directories exist
	try {
		if (const fs::path p(pat); p.has_parent_path()) {
			fs::create_directories(p.parent_path());
		}
	} catch (const fs::filesystem_error&) {
		// failed to create parent directories
		return false;
	}

	// Open file for writing in binary mode
	std::ofstream ofs(pat, std::ios::binary | std::ios::out | std::ios::trunc);
	if (!ofs.is_open()) {
		return false;
	}

	ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
	return ofs.good();
}

editor::ResourceSlot::Entry ResourceManager::CreateResourceSlotEntry(const std::filesystem::path& path) {
	if (!g_fileIcon) {
		g_fileIcon = resource::ResourceManager::GetInstance()->LoadResource<Texture>("editor/icons/genericFile.png");
		g_jsonIcon = resource::ResourceManager::GetInstance()->LoadResource<Texture>("editor/icons/jsonFile.png");
		g_objIcon = resource::ResourceManager::GetInstance()->LoadResource<Texture>("editor/icons/objFile.png");
	}

	editor::ResourceSlot::Entry e;
	e.isDirectory = false;
	// if (ec) rel = de.path().filename();
	e.relativePath = path;
	e.name = path.filename().string();
	e.extension = path.has_extension() ? path.extension().string() : std::string();
	std::ranges::transform(e.extension, e.extension.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});

	// icon
	if (e.extension == ".png" || e.extension == ".jpg") {
		std::string normalized = e.relativePath.string();
		std::replace(normalized.begin(), normalized.end(), '\\', '/');
		e.icon = resource::ResourceManager::GetInstance()->LoadResource<Texture>(normalized);
	} else if (e.extension == ".json") {
		e.icon = g_jsonIcon;
	} else if (e.extension == ".obj") {
		e.icon = g_objIcon;
	} else {
		e.icon = g_fileIcon;
	}
	if (!e.icon) {
		// generic
		e.icon = g_fileIcon;
	}

	return e;
}

}
