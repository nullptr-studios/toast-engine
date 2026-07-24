#include "ui_file_interface.hpp"

#include "document_preprocess.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <toast/assets/asset_manager.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

namespace ui {

UIFileInterface::UIFileInterface(ColorResolver color_resolver) : m_color_resolver(std::move(color_resolver)) { }

auto UIFileInterface::Open(const Rml::String& path) -> Rml::FileHandle {
	ZoneScoped;

	if (path.find("://") == Rml::String::npos) {
		TOAST_WARN("UI", "Refusing to open '{}': UI resources must use asset paths", path);
		return 0;
	}

	auto bytes = assets::AssetManager::get().tryLoadBytes(path);
	if (!bytes) {
		TOAST_WARN("UI", "Failed to open '{}'", path);
		return 0;
	}

	constexpr std::string_view rcss_extension = ".rcss";
	const bool is_rcss = path.size() >= rcss_extension.size() &&
	                     std::equal(rcss_extension.rbegin(), rcss_extension.rend(), path.rbegin(), [](char a, char b) {
		                     return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
	                     });
	if (is_rcss) {
		const std::string_view source(reinterpret_cast<const char*>(bytes->data()), bytes->size());
		std::string transformed = resolveColorReferences(source, m_color_resolver);
		bytes = std::vector<uint8_t>(transformed.begin(), transformed.end());
	}

	const Rml::FileHandle handle = m_next_handle++;
	m_open_files.emplace(handle, OpenFile {.data = std::move(*bytes), .cursor = 0});
	return handle;
}

void UIFileInterface::Close(Rml::FileHandle file) {
	m_open_files.erase(file);
}

auto UIFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file) -> size_t {
	auto it = m_open_files.find(file);
	if (it == m_open_files.end()) {
		return 0;
	}

	auto& [data, cursor] = it->second;
	const size_t available = data.size() - cursor;
	const size_t count = std::min(size, available);
	std::memcpy(buffer, data.data() + cursor, count);
	cursor += count;
	return count;
}

auto UIFileInterface::Seek(Rml::FileHandle file, long offset, int origin) -> bool {
	auto it = m_open_files.find(file);
	if (it == m_open_files.end()) {
		return false;
	}

	auto& [data, cursor] = it->second;
	int64_t target = 0;
	switch (origin) {
		case SEEK_SET: target = offset; break;
		case SEEK_CUR: target = static_cast<int64_t>(cursor) + offset; break;
		case SEEK_END: target = static_cast<int64_t>(data.size()) + offset; break;
		default: return false;
	}

	if (target < 0 || static_cast<size_t>(target) > data.size()) {
		return false;
	}
	cursor = static_cast<size_t>(target);
	return true;
}

auto UIFileInterface::Tell(Rml::FileHandle file) -> size_t {
	auto it = m_open_files.find(file);
	return it != m_open_files.end() ? it->second.cursor : 0;
}

auto UIFileInterface::Length(Rml::FileHandle file) -> size_t {
	auto it = m_open_files.find(file);
	return it != m_open_files.end() ? it->second.data.size() : 0;
}

}
