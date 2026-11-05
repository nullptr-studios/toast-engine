#include "toast_pack/pack.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <format>
#include <fstream>
#include <lz4.h>
#include <lz4hc.h>
#include <stdexcept>

namespace pack {

namespace {

[[noreturn]]
void fail(const std::string& msg) {
	throw std::runtime_error(msg);
}

template<typename T>
void putLE(std::ostream& w, T value) {
	uint8_t buf[sizeof(T)];
	for (std::size_t i = 0; i < sizeof(T); ++i) {
		buf[i] = static_cast<uint8_t>((static_cast<uint64_t>(value) >> (8 * i)) & 0xFF);
	}
	w.write(reinterpret_cast<const char*>(buf), sizeof(T));
	if (!w) {
		fail("write failed");
	}
}

template<typename T>
auto getLE(std::istream& r) -> T {
	uint8_t buf[sizeof(T)];
	r.read(reinterpret_cast<char*>(buf), sizeof(T));
	if (!r) {
		fail("unexpected end of file");
	}
	uint64_t value = 0;
	for (std::size_t i = 0; i < sizeof(T); ++i) {
		value |= static_cast<uint64_t>(buf[i]) << (8 * i);
	}
	return static_cast<T>(value);
}

auto readEntryFrom(std::istream& r) -> Entry {
	Entry e;
	e.hash = getLE<uint64_t>(r);

	const auto path_len = getLE<uint32_t>(r);
	if (path_len > MAX_PATH_LEN) {
		fail(std::format("path_len {} is unreasonably large", path_len));
	}
	e.rel.resize(path_len);
	r.read(e.rel.data(), path_len);
	if (!r) {
		fail("unexpected end of file");
	}

	e.offset = getLE<uint64_t>(r);
	e.orig_size = getLE<uint64_t>(r);
	e.stored_size = getLE<uint64_t>(r);
	e.flags = getLE<uint8_t>(r);
	return e;
}

}

void writeHeader(std::ostream& w, const Header& header) {
	w.write(reinterpret_cast<const char*>(MAGIC.data()), MAGIC.size());
	putLE<uint16_t>(w, VERSION);
	putLE<uint32_t>(w, header.file_count);
	putLE<uint64_t>(w, header.file_table_offset);
}

auto readHeader(std::istream& r) -> Header {
	std::array<uint8_t, 6> magic {};
	r.read(reinterpret_cast<char*>(magic.data()), magic.size());
	if (!r) {
		fail("unexpected end of file");
	}
	if (magic != MAGIC) {
		fail("bad magic: expected \"PACK\\0\\0\"");
	}

	const auto version = getLE<uint16_t>(r);
	if (version != VERSION) {
		fail(std::format("unsupported version {}, expected {}", version, VERSION));
	}

	Header h;
	h.file_count = getLE<uint32_t>(r);
	h.file_table_offset = getLE<uint64_t>(r);
	return h;
}

void writeTable(std::ostream& w, const std::vector<Entry>& entries) {
	putLE<uint32_t>(w, static_cast<uint32_t>(entries.size()));
	for (const auto& e : entries) {
		putLE<uint64_t>(w, e.hash);
		putLE<uint32_t>(w, static_cast<uint32_t>(e.rel.size()));
		w.write(e.rel.data(), static_cast<std::streamsize>(e.rel.size()));
		putLE<uint64_t>(w, e.offset);
		putLE<uint64_t>(w, e.orig_size);
		putLE<uint64_t>(w, e.stored_size);
		putLE<uint8_t>(w, e.flags);
	}
	if (!w) {
		fail("write failed");
	}
}

auto readTable(std::istream& r, uint32_t expected_count) -> std::vector<Entry> {
	const auto count = getLE<uint32_t>(r);
	if (count != expected_count) {
		fail(std::format("table file_count {} doesn't match header {}", count, expected_count));
	}
	std::vector<Entry> entries;
	entries.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		entries.push_back(readEntryFrom(r));
	}
	return entries;
}

auto fnv1a64(std::string_view s) -> uint64_t {
	constexpr uint64_t basis = 14695981039346656037ULL;
	constexpr uint64_t prime = 1099511628211ULL;
	uint64_t h = basis;
	for (const unsigned char c : s) {
		h ^= static_cast<uint64_t>(c);
		h *= prime;
	}
	return h;
}

auto canonicalRel(const std::filesystem::path& path, const std::filesystem::path& base) -> std::string {
	const auto rel = path.lexically_relative(base);
	if (rel.empty() || *rel.begin() == "..") {
		fail(std::format("'{}' is not under base '{}'", path.string(), base.string()));
	}
	const auto u8 = rel.generic_u8string();
	return {reinterpret_cast<const char*>(u8.data()), u8.size()};
}

auto collectFiles(const std::filesystem::path& dir) -> std::vector<std::filesystem::path> {
	namespace fs = std::filesystem;
	std::vector<fs::path> files;
	for (const auto& entry : fs::recursive_directory_iterator(dir)) {
		if (entry.is_regular_file()) {
			files.push_back(entry.path());
		}
	}
	std::ranges::sort(files);
	return files;
}

auto tryCompress(std::span<const uint8_t> data) -> std::optional<std::vector<uint8_t>> {
	if (data.empty() || data.size() > static_cast<std::size_t>(LZ4_MAX_INPUT_SIZE)) {
		return std::nullopt;
	}
	const int src_size = static_cast<int>(data.size());
	std::vector<uint8_t> out(static_cast<std::size_t>(LZ4_compressBound(src_size)));
	const int n = LZ4_compress_HC(
	    reinterpret_cast<const char*>(data.data()),
	    reinterpret_cast<char*>(out.data()),
	    src_size,
	    static_cast<int>(out.size()),
	    12
	);
	if (n <= 0 || static_cast<std::size_t>(n) + 8 >= data.size()) {
		return std::nullopt;
	}
	out.resize(static_cast<std::size_t>(n));
	return out;
}

auto decompress(std::span<const uint8_t> data, uint64_t orig_size) -> std::vector<uint8_t> {
	if (orig_size > static_cast<uint64_t>(INT_MAX) || data.size() > static_cast<std::size_t>(INT_MAX)) {
		fail("LZ4 decompress failed: block too large");
	}
	std::vector<uint8_t> out(static_cast<std::size_t>(orig_size));
	const int n = LZ4_decompress_safe(
	    reinterpret_cast<const char*>(data.data()),
	    reinterpret_cast<char*>(out.data()),
	    static_cast<int>(data.size()),
	    static_cast<int>(orig_size)
	);
	if (n < 0 || static_cast<uint64_t>(n) != orig_size) {
		fail(std::format("LZ4 decompress failed (got {}, expected {} bytes)", n, orig_size));
	}
	return out;
}

auto readBlobAt(std::istream& r, uint64_t offset, uint64_t size) -> std::vector<uint8_t> {
	r.clear();
	r.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	std::vector<uint8_t> buf(static_cast<std::size_t>(size));
	r.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));
	if (!r) {
		fail(std::format("cannot read {} bytes at offset {}", size, offset));
	}
	return buf;
}

PackReader::PackReader(const std::filesystem::path& path) : m_path(path) {
	std::ifstream f(path, std::ios::binary);
	if (!f.is_open()) {
		fail("PackReader: cannot open " + path.string());
	}
	try {
		const auto header = readHeader(f);
		f.seekg(static_cast<std::streamoff>(header.file_table_offset), std::ios::beg);
		if (!f) {
			fail("cannot seek to file table");
		}
		m_list = readTable(f, header.file_count);
		m_index.reserve(m_list.size());
		for (std::size_t i = 0; i < m_list.size(); ++i) {
			m_index.emplace(m_list[i].hash, i);
		}
	} catch (const std::exception& ex) {
		fail("PackReader: " + std::string(ex.what()) + " in " + path.string());
	}
}

auto PackReader::findByHash(uint64_t hash) const -> const Entry* {
	const auto it = m_index.find(hash);
	return it == m_index.end() ? nullptr : &m_list[it->second];
}

auto PackReader::find(std::string_view rel_path) const -> const Entry* {
	std::string key(rel_path);
	std::ranges::replace(key, '\\', '/');
	const Entry* e = findByHash(fnv1a64(key));
	return (e && e->rel == key) ? e : nullptr;
}

auto PackReader::readEntry(const Entry& entry) const -> std::vector<uint8_t> {
	std::ifstream f(m_path, std::ios::binary);
	if (!f.is_open()) {
		fail("PackReader: cannot re-open " + m_path.string());
	}
	auto blob = readBlobAt(f, entry.offset, entry.stored_size);
	return entry.isCompressed() ? decompress(blob, entry.orig_size) : blob;
}

}
