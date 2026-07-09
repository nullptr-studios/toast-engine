#include "pack.hpp"

#include <cstring>
#include <fstream>
#include <lz4.h>
#include <stdexcept>
#include <toast/log.hpp>

namespace assets {

namespace {
constexpr uint8_t k_magic[6] = {'P', 'A', 'C', 'K', '\0', '\0'};
constexpr uint16_t k_version = 2;

template<typename T>
auto read_le(std::ifstream& f) -> T {
	T val {};
	f.read(reinterpret_cast<char*>(&val), sizeof(T));
	return val;    // little-endian; host is LE on x86/ARM Linux/Windows
}
}

auto PackArchive::fnv1a64(std::string_view s) -> uint64_t {
	constexpr uint64_t basis = 14695981039346656037ULL;
	constexpr uint64_t prime = 1099511628211ULL;
	uint64_t h = basis;
	for (const unsigned char c : s) {
		h ^= static_cast<uint64_t>(c);
		h *= prime;
	}
	return h;
}

PackArchive::PackArchive(const std::filesystem::path& path) : m_path(path) {
	std::ifstream f(path, std::ios::binary);
	if (!f.is_open()) {
		throw std::runtime_error("PackArchive: cannot open " + path.string());
	}

	// Header
	uint8_t magic[6] {};
	f.read(reinterpret_cast<char*>(magic), 6);
	if (std::memcmp(magic, k_magic, 6) != 0) {
		throw std::runtime_error("PackArchive: bad magic in " + path.string());
	}

	const uint16_t version = read_le<uint16_t>(f);
	if (version != k_version) {
		throw std::runtime_error("PackArchive: unsupported version " + std::to_string(version));
	}

	const uint32_t file_count = read_le<uint32_t>(f);
	const uint64_t table_offset = read_le<uint64_t>(f);

	// Seek to file table
	f.seekg(static_cast<std::streamoff>(table_offset), std::ios::beg);
	if (!f) {
		throw std::runtime_error("PackArchive: cannot seek to file table in " + path.string());
	}

	const uint32_t table_count = read_le<uint32_t>(f);
	if (table_count != file_count) {
		throw std::runtime_error("PackArchive: file_count mismatch in " + path.string());
	}

	m_entries.reserve(file_count);

	for (uint32_t i = 0; i < file_count; ++i) {
		Entry e;
		const uint64_t hash = read_le<uint64_t>(f);
		const uint32_t path_len = read_le<uint32_t>(f);
		e.rel_path.resize(path_len);
		f.read(e.rel_path.data(), path_len);
		e.offset = read_le<uint64_t>(f);
		e.orig_size = read_le<uint64_t>(f);
		e.stored_size = read_le<uint64_t>(f);
		e.flags = read_le<uint8_t>(f);

		m_entries.emplace(hash, std::move(e));
	}

	TOAST_INFO("PackArchive", "Mounted {} ({} entries)", path.string(), m_entries.size());
}

auto PackArchive::read(std::string_view rel_path) const -> std::optional<std::vector<uint8_t>> {
	// Normalise to forward slashes before hashing
	std::string key(rel_path);
	for (auto& c : key) {
		if (c == '\\') {
			c = '/';
		}
	}

	const uint64_t hash = fnv1a64(key);
	const auto it = m_entries.find(hash);
	if (it == m_entries.end()) {
		return std::nullopt;
	}

	// Verify path
	const Entry& e = it->second;
	if (e.rel_path != key) {
		TOAST_WARN("PackArchive", "Hash collision for '{}' vs '{}' in {}", key, e.rel_path, m_path.string());
		return std::nullopt;
	}

	std::ifstream f(m_path, std::ios::binary);
	if (!f.is_open()) {
		TOAST_ERROR("PackArchive", "Cannot re-open pack for reading: {}", m_path.string());
		return std::nullopt;
	}

	f.seekg(static_cast<std::streamoff>(e.offset), std::ios::beg);
	if (!f) {
		TOAST_ERROR("PackArchive", "Seek failed in {}", m_path.string());
		return std::nullopt;
	}

	std::vector<uint8_t> stored(e.stored_size);
	f.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(e.stored_size));
	if (!f) {
		TOAST_ERROR("PackArchive", "Read failed in {}", m_path.string());
		return std::nullopt;
	}

	if (!(e.flags & k_flag_compressed)) {
		return stored;
	}

	// LZ4 decompress
	std::vector<uint8_t> out(e.orig_size);
	const int result = LZ4_decompress_safe(
	    reinterpret_cast<const char*>(stored.data()),
	    reinterpret_cast<char*>(out.data()),
	    static_cast<int>(e.stored_size),
	    static_cast<int>(e.orig_size)
	);

	if (result < 0 || static_cast<uint64_t>(result) != e.orig_size) {
		TOAST_ERROR("PackArchive", "LZ4 decompression failed for '{}' in {}", key, m_path.string());
		return std::nullopt;
	}

	return out;
}

}
