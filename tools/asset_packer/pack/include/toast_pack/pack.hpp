/**
 * @file pack.hpp
 * @author Xein
 * @date 7 Jul 2026
 * @brief Shared PACK v2 format definitions, reader and writer helpers (used by the asset packer tools and the engine)
 *
 * On-disk format (little-endian, no padding):
 *
 * [ Header - 20 bytes ]
 *   magic[6]            "PACK\0\0"
 *   version u16         2
 *   file_count u32
 *   file_table_offset u64
 *
 * [ File data blocks: one raw or LZ4-compressed blob per file ]
 *
 * [ File table at file_table_offset ]
 *   file_count u32
 *   For each entry (sorted by hash, then rel path):
 *     hash u64            FNV-1a-64 of the forward-slash relative path
 *     path_len u32
 *     path[path_len]      UTF-8, forward slashes
 *     offset u64          byte offset of this file's data block
 *     orig_size u64       uncompressed length
 *     stored_size u64     bytes written (compressed or raw)
 *     flags u8            bit 0: LZ4-compressed
 *
 * All functions report errors by throwing std::runtime_error.
 */

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pack {

inline constexpr std::array<uint8_t, 6> MAGIC = {'P', 'A', 'C', 'K', '\0', '\0'};
inline constexpr uint16_t VERSION = 2;
inline constexpr uint64_t HEADER_SIZE = 20;
inline constexpr uint8_t FLAG_COMPRESSED = 1;
inline constexpr uint32_t MAX_PATH_LEN = 4096;

struct Header {
	uint32_t file_count = 0;
	uint64_t file_table_offset = 0;
};

struct Entry {
	uint64_t hash = 0;
	std::string rel;
	uint64_t offset = 0;
	uint64_t orig_size = 0;
	uint64_t stored_size = 0;
	uint8_t flags = 0;

	[[nodiscard]]
	auto isCompressed() const -> bool {
		return (flags & FLAG_COMPRESSED) != 0;
	}
};

void writeHeader(std::ostream& w, const Header& header);
auto readHeader(std::istream& r) -> Header;

void writeTable(std::ostream& w, const std::vector<Entry>& entries);
auto readTable(std::istream& r, uint32_t expected_count) -> std::vector<Entry>;

[[nodiscard]]
auto fnv1a64(std::string_view s) -> uint64_t;

/// Relative path of @c path under @c base
[[nodiscard]]
auto canonicalRel(const std::filesystem::path& path, const std::filesystem::path& base) -> std::string;

/// All regular files under @c dir
[[nodiscard]]
auto collectFiles(const std::filesystem::path& dir) -> std::vector<std::filesystem::path>;

[[nodiscard]]
auto tryCompress(std::span<const uint8_t> data) -> std::optional<std::vector<uint8_t>>;

[[nodiscard]]
auto decompress(std::span<const uint8_t> data, uint64_t orig_size) -> std::vector<uint8_t>;

/// Read @p size bytes at @p offset
[[nodiscard]]
auto readBlobAt(std::istream& r, uint64_t offset, uint64_t size) -> std::vector<uint8_t>;

/// Read-only view of a .pak
class PackReader {
public:
	explicit PackReader(const std::filesystem::path& path);

	[[nodiscard]]
	auto path() const -> const std::filesystem::path& {
		return m_path;
	}

	[[nodiscard]]
	auto entryCount() const -> std::size_t {
		return m_list.size();
	}

	/// All entries in table order
	[[nodiscard]]
	auto entries() const -> std::span<const Entry> {
		return m_list;
	}

	/// Entry whose path hash matches
	[[nodiscard]]
	auto findByHash(uint64_t hash) const -> const Entry*;

	/// Entry for a relative path
	[[nodiscard]]
	auto find(std::string_view rel_path) const -> const Entry*;

	/// Stored blob of @p entry
	[[nodiscard]]
	auto readEntry(const Entry& entry) const -> std::vector<uint8_t>;

private:
	std::filesystem::path m_path;
	std::vector<Entry> m_list;
	std::unordered_map<uint64_t, std::size_t> m_index;
};

}
