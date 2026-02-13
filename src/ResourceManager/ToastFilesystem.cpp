/// @file ToastFileSystem.cpp
/// @brief File system abstraction (filesystem + pack) shared between ResourceManager and Ultralight.

#include <Toast/Resources/ToastFileSystem.hpp>

#include <Toast/Log.hpp>
#include <Toast/Profiler.hpp>

#include <Ultralight/Buffer.h>

#include <algorithm>
#include <ranges>
#include <cctype>
#include <cstring>
#include <fstream>

namespace {
void DestroyBuffer(void* /*user_data*/, void* data) {
	delete[] static_cast<char*>(data);
}
}

ToastFileSystem* ToastFileSystem::instance = nullptr;

ToastFileSystem::ToastFileSystem() = default;

ToastFileSystem& ToastFileSystem::Get() {
	if (!instance) {
		instance = new ToastFileSystem();
	}
	return *instance;
}

std::string ToastFileSystem::NormalizePath(const std::string& path) const {
	if (path.find("assets/") == std::string::npos) {
		return std::string("assets/") + path;
	}
	return path;
}

bool ToastFileSystem::UsePackFile(const std::string_view& path) {
	TOAST_INFO("[ToastFileSystem] Using pack file: {}", path);
	packEnabled = packFile.Open(path);
	return packEnabled;
}

void ToastFileSystem::ClosePackFile() {
	if (packEnabled) {
		packFile.Close();
		packEnabled = false;
	}
}

bool ToastFileSystem::OpenFile(const std::string& path, std::istringstream& data_out) const {
	std::vector<uint8_t> data;
	if (!OpenFile(path, data)) {
		return false;
	}
	data_out.str(std::string(reinterpret_cast<const char*>(data.data()), data.size()));
	return true;
}

bool ToastFileSystem::OpenFile(const std::string& path, std::vector<uint8_t>& data_out) const {
	PROFILE_ZONE;

	if (packEnabled) {
		try {
			return packFile.ReadFile(path, data_out);
		} catch (const std::exception& e) {
			TOAST_ERROR("Pack read failed for {}: {}", path, e.what());
			return false;
		}
	}

	const std::string normalized = NormalizePath(path);
	std::ifstream ifs(normalized, std::ios::binary);
	if (!ifs) {
		return false;
	}
	ifs.seekg(0, std::ios::end);
	const std::streamsize size = ifs.tellg();
	ifs.seekg(0, std::ios::beg);
	data_out.resize(static_cast<size_t>(size));
	ifs.read(reinterpret_cast<char*>(data_out.data()), size);
	return true;
}

bool ToastFileSystem::FileExists(const ultralight::String& path) {
	std::string filepath = path.utf8().data();
	if (packEnabled) {
		return packFile.FileExists(filepath);
	}
	std::ifstream f(NormalizePath(filepath), std::ios::binary);
	return static_cast<bool>(f);
}

ultralight::String ToastFileSystem::GetFileMimeType(const ultralight::String& path) {
	std::string filepath = path.utf8().data();
	auto dot_pos = filepath.rfind('.');
	if (dot_pos == std::string::npos) return "application/unknown";
	std::string ext = filepath.substr(dot_pos + 1);
	std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (ext == "html" || ext == "htm") return "text/html";
	if (ext == "js") return "application/javascript";
	if (ext == "css") return "text/css";
	if (ext == "json") return "application/json";
	if (ext == "xml") return "application/xml";
	if (ext == "txt") return "text/plain";
	if (ext == "png") return "image/png";
	if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
	if (ext == "gif") return "image/gif";
	if (ext == "svg") return "image/svg+xml";
	if (ext == "ico") return "image/x-icon";
	if (ext == "webp") return "image/webp";
	if (ext == "woff") return "font/woff";
	if (ext == "woff2") return "font/woff2";
	if (ext == "ttf") return "font/ttf";
	if (ext == "otf") return "font/otf";
	if (ext == "eot") return "application/vnd.ms-fontobject";
	if (ext == "mp3") return "audio/mpeg";
	if (ext == "wav") return "audio/wav";
	if (ext == "ogg") return "audio/ogg";
	if (ext == "mp4") return "video/mp4";
	if (ext == "webm") return "video/webm";
	if (ext == "pdf") return "application/pdf";
	if (ext == "zip") return "application/zip";
	if (ext == "dat") return "application/octet-stream";
	return "application/unknown";
}

ultralight::RefPtr<ultralight::Buffer> ToastFileSystem::OpenFile(const ultralight::String& path) {
	std::string filepath = path.utf8().data();
	std::vector<uint8_t> data;
	if (!OpenFile(filepath, data)) {
		return nullptr;
	}

	auto* buffer_data = new char[data.size()];
	std::memcpy(buffer_data, data.data(), data.size());
	return ultralight::Buffer::Create(buffer_data, data.size(), &DestroyBuffer, nullptr);
}
