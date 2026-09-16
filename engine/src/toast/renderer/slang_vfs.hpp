// @file slang_vfs.hpp
// @author Xein
// @date 17 Jul 2026

#pragma once

#include <slang.h>
#include <string>
#include <string_view>
#include <vector>

namespace renderer {

class SlangVfs final : public ISlangFileSystem {
public:
	static auto get() -> SlangVfs&;

	SLANG_NO_THROW auto SLANG_MCALL queryInterface(const SlangUUID& uuid, void** out_object) -> SlangResult override;

	SLANG_NO_THROW auto SLANG_MCALL addRef() -> uint32_t override { return 1; }

	SLANG_NO_THROW auto SLANG_MCALL release() -> uint32_t override { return 1; }

	SLANG_NO_THROW auto SLANG_MCALL castAs(const SlangUUID& uuid) -> void* override;

	SLANG_NO_THROW auto SLANG_MCALL loadFile(const char* path, ISlangBlob** out_blob) -> SlangResult override;

	static auto normalizeUri(std::string_view path) -> std::string;

	static auto makeBlob(const void* data, size_t size) -> ISlangBlob*;

	/**
	 * @brief Records every URI resolved while alive. Slang getDependencyFileCount() is empty for modules loaded from source
	 * @note Not thread safe
	 */
	class Recorder {
	public:
		Recorder();
		~Recorder();

		Recorder(const Recorder&) = delete;
		auto operator=(const Recorder&) -> Recorder& = delete;
		Recorder(Recorder&&) = delete;
		auto operator=(Recorder&&) -> Recorder& = delete;

		[[nodiscard]]
		auto resolved() const -> const std::vector<std::string>& {
			return m_resolved;
		}

	private:
		friend class SlangVfs;

		void record(std::string uri);

		std::vector<std::string> m_resolved;
	};

private:
	static inline Recorder* s_recorder = nullptr;
};

}
