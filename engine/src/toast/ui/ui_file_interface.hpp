/**
 * @file ui_file_interface.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Routes RmlUi file requests through the asset manager VFS
 */

#pragma once
#include <RmlUi/Core/FileInterface.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ui {

/**
 * @brief RmlUi file interface backed by the engine VFS
 *
 * Every path RmlUi asks for is resolved through AssetManager::loadBytes(),
 * so `assets://`, `core://` and any mounted database or pack works fine
 */
class UIFileInterface final : public Rml::FileInterface {
public:
	auto Open(const Rml::String& path) -> Rml::FileHandle override;
	void Close(Rml::FileHandle file) override;

	auto Read(void* buffer, size_t size, Rml::FileHandle file) -> size_t override;
	auto Seek(Rml::FileHandle file, long offset, int origin) -> bool override;
	auto Tell(Rml::FileHandle file) -> size_t override;
	auto Length(Rml::FileHandle file) -> size_t override;

private:
	struct OpenFile {
		std::vector<uint8_t> data;
		size_t cursor = 0;
	};

	Rml::FileHandle m_next_handle = 1;
	std::unordered_map<Rml::FileHandle, OpenFile> m_open_files;
};

}
