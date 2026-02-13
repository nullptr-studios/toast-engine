/// @file ToastFileSystem.hpp
/// @author dario
/// @date 13/02/2026.

#pragma once
#include "ResourceManager/PackLoader.hpp"
#include "Ultralight/platform/FileSystem.h"
#include <string>
#include <string_view>
#include <sstream>
#include <vector>

class ToastFileSystem : public ultralight::FileSystem {
	ToastFileSystem();
	~ToastFileSystem() override = default;
	
	mutable resource::PackFile packFile; // Instance of the pack file for resource loading
	bool packEnabled = false;

	static ToastFileSystem* instance; // Singleton instance

	// Normalize input path and ensure it points under assets/ when using filesystem
	[[nodiscard]] std::string NormalizePath(const std::string& path) const;

public:
	// Singleton accessor
	static ToastFileSystem& Get();

	// Enable reading from a .pkg pack file. Returns true on success.
	bool UsePackFile(const std::string_view& path);
	// Disable pack reading and close pack file if open.
	void ClosePackFile();

    bool FileExists(const ultralight::String& path) override;

    ultralight::String GetFileMimeType(const ultralight::String& path) override;

    ultralight::String GetFileCharset(const ultralight::String& path) override {
        // Default to UTF-8 for text files
        return "utf-8";
    }

    ultralight::RefPtr<ultralight::Buffer> OpenFile(const ultralight::String& path) override;

	// Engine-facing helpers (non-Ultralight)
	bool OpenFile(const std::string& path, std::istringstream& data_out) const;
	bool OpenFile(const std::string& path, std::vector<uint8_t>& data_out) const;
};