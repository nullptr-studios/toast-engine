/**
 * @file pack.hpp
 * @author Xein
 * @date 8 Jul 2026
 * @brief Read-side parser for the PACK v2 archive format produced by tools/asset_packer
 *
 * Format (little-endian):
 *   Header  (20 bytes):  magic[6] "PACK\0\0"  |  version u16 = 2
 *                        file_count u32        |  file_table_offset u64
 *   Data blobs:          raw or LZ4-compressed bytes per entry
 *   File table:          file_count u32
 *                        per entry (sorted by hash then path):
 *                          hash u64        FNV-1a-64 of the forward-slash relative path
 *                          path_len u32
 *                          path[path_len]  UTF-8
 *                          offset u64      byte offset of data block
 *                          orig_size u64
 *                          stored_size u64
 *                          flags u8        bit 0 = LZ4-compressed
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

class PackArchive {
public:
	explicit PackArchive(const std::filesystem::path& path);

	[[nodiscard]]
	auto read(std::string_view rel_path) const -> std::optional<std::vector<uint8_t>>;

	[[nodiscard]]
	auto path() const -> const std::filesystem::path& {
		return m_path;
	}

	[[nodiscard]]
	auto entryCount() const -> std::size_t {
		return m_entries.size();
	}

private:
	static constexpr uint8_t k_flag_compressed = 1;

	struct Entry {
		std::string rel_path;
		uint64_t offset;
		uint64_t orig_size;
		uint64_t stored_size;
		uint8_t flags;
	};

	static auto fnv1a64(std::string_view s) -> uint64_t;

	std::filesystem::path m_path;
	std::unordered_map<uint64_t, Entry> m_entries;
};

}
