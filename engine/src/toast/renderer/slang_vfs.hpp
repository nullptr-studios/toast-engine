/*
 * @file slang_vfs.hpp
 * @author Xein
 * @date 17 Jul 2026
 */

#pragma once

#include <slang.h>
#include <string>
#include <string_view>

namespace renderer {

/**
 * @class SlangVfs
 * @brief ISlangFileSystem implementation resolving virtual URIs through the AssetManager
 *
 * Lets @c import and @c #include inside .slang modules resolve against the engine VFS
 */
class SlangVfs final : public ISlangFileSystem {
public:
	static auto get() -> SlangVfs&;

	// ISlangUnknown
	SLANG_NO_THROW auto SLANG_MCALL queryInterface(const SlangUUID& uuid, void** out_object) -> SlangResult override;

	SLANG_NO_THROW auto SLANG_MCALL addRef() -> uint32_t override { return 1; }

	SLANG_NO_THROW auto SLANG_MCALL release() -> uint32_t override { return 1; }

	// ISlangCastable
	SLANG_NO_THROW auto SLANG_MCALL castAs(const SlangUUID& uuid) -> void* override;

	// ISlangFileSystem
	SLANG_NO_THROW auto SLANG_MCALL loadFile(const char* path, ISlangBlob** out_blob) -> SlangResult override;

	/// Rebuilds a clean "pack://relative" URI from paths Slang may have mangled
	static auto normalizeUri(std::string_view path) -> std::string;

	/// Creates an arc ISlangBlob owning a copy of the given bytes
	static auto makeBlob(const void* data, size_t size) -> ISlangBlob*;
};

}
