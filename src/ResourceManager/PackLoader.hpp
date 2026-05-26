#pragma once

#include "Toast/Log.hpp"
#include "Toast/Profiler.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <lz4.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace resource {
namespace fs = std::filesystem;

struct PackHeader {
	std::array<char, 9> magic;
	uint32_t version;
	uint32_t fileCount;
	uint64_t fileTableOffset;
};

static inline std::u8string canonical_path_for_pack(const fs::path& p) {
	PROFILE_ZONE;
	fs::path normalized = p.lexically_normal();
	std::u8string s = normalized.generic_u8string();
	std::ranges::replace(s, '\\', '/');
	if (s.size() >= 2 && s[0] == '.' && s[1] == '/') {
		s.erase(0, 2);
	}

	return s;
}

inline uint64_t fnv1a_hash64(const std::u8string& s) {
	PROFILE_ZONE;
	uint64_t hash = 14695981039346656037ULL;
	for (unsigned char c : s) {
		hash ^= static_cast<uint64_t>(c);
		hash *= 1099511628211ULL;
	}
	return hash;
}

struct FileEntry {
	uint64_t offset;
	uint64_t origSize;
	uint64_t storedSize;
	uint8_t flags;
};

struct HashMapEntry {
	size_t index;
	uint64_t hash;
};

class PackFile {
public:
	bool Open(const std::string_view& pack_path) {
		PROFILE_ZONE;

		m_in.open(pack_path.data(), std::ios::binary);
		if (!m_in) {
			return false;
		}

		// buffer
		m_in.rdbuf()->pubsetbuf(nullptr, 65536);

		PackHeader h {};
		m_in.read(reinterpret_cast<char*>(&h), sizeof(h));
		if (!m_in) {
			return false;
		}

		if (memcmp(h.magic.data(), "TOASTPACK", 9) != 0) {
			throw ToastException("Invalid pack file magic!?!?!?!");
		}
		m_header = h;

		// read table
		m_in.seekg(static_cast<std::streamoff>(m_header.fileTableOffset));
		uint32_t file_count = 0;
		m_in.read(reinterpret_cast<char*>(&file_count), sizeof(file_count));
		if (!m_in) {
			return false;
		}

		m_entries.clear();
		m_lookupMap.clear();
		m_entries.reserve(file_count);
		m_lookupMap.reserve(file_count);

		for (uint32_t i = 0; i < file_count; ++i) {
			uint64_t hsh = 0;
			uint32_t path_len = 0;
			m_in.read(reinterpret_cast<char*>(&hsh), sizeof(hsh));
			m_in.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));
			
			std::u8string path;
			path.resize(path_len);
			m_in.read(reinterpret_cast<char*>(path.data()), path_len);

			uint64_t offset = 0, orig = 0, stored = 0;
			uint8_t f = 0;
			m_in.read(reinterpret_cast<char*>(&offset), sizeof(offset));
			m_in.read(reinterpret_cast<char*>(&orig), sizeof(orig));
			m_in.read(reinterpret_cast<char*>(&stored), sizeof(stored));
			m_in.read(reinterpret_cast<char*>(&f), sizeof(f));

			if (!m_in) {
				TOAST_ERROR("Failed reading table entry {0}", i);
				return false;
			}

			// Store entry
			size_t entry_idx = m_entries.size();
			m_entries.emplace_back(FileEntry{offset, orig, stored, f});
			
			// Add to hash map
			m_lookupMap[path] = HashMapEntry{entry_idx, hsh};
		}

		return true;
	}

	bool FileExists(const std::string_view raw_path) {
		PROFILE_ZONE;
		
		std::u8string path = canonicalize_path(raw_path);
		
		return m_lookupMap.find(path) != m_lookupMap.end();
	}

	void Close() {
		if (m_in.is_open()) {
			m_in.close();
		}
		m_entries.clear();
		m_lookupMap.clear();
	}

	bool ReadFile(std::string_view raw_path, std::vector<uint8_t>& out) {
		PROFILE_ZONE;

		std::u8string path = canonicalize_path(raw_path);

		auto it = m_lookupMap.find(path);
		if (it == m_lookupMap.end()) {
			TOAST_ERROR("PackFile: Path {0} not found", raw_path);
			return false;
		}

		size_t idx = it->second.index;
		std::lock_guard<std::mutex> lock(m_readMtx);
		return read_at_index(idx, out);
	}

private:
	std::u8string canonicalize_path(std::string_view raw_path) {
		std::string raw_path_str(raw_path);
		
		if (raw_path_str.find("assets/") == 0) {
			raw_path_str = raw_path_str.substr(7);
		}
		
		return canonical_path_for_pack(raw_path_str);
	}

	bool read_at_index(size_t i, std::vector<uint8_t>& out) {
		PROFILE_ZONE;

		if (i >= m_entries.size()) {
			TOAST_ERROR("PackFile: Invalid entry index {0}", i);
			return false;
		}

		const FileEntry& entry = m_entries[i];

		// Reuse decompression buffer if possible
		if (m_decompBuf.size() < entry.storedSize) {
			m_decompBuf.resize(static_cast<size_t>(entry.storedSize));
		}

		m_in.seekg(static_cast<std::streamoff>(entry.offset));
		m_in.read(reinterpret_cast<char*>(m_decompBuf.data()), static_cast<std::streamsize>(entry.storedSize));
		if (!m_in) {
			TOAST_ERROR("Read error at offset {0}", entry.offset);
			return false;
		}

		if (entry.flags & 1) {
			out.resize(static_cast<size_t>(entry.origSize));
			int got = LZ4_decompress_safe(
			    reinterpret_cast<const char*>(m_decompBuf.data()), 
			    reinterpret_cast<char*>(out.data()), 
			    static_cast<int>(entry.storedSize), 
			    static_cast<int>(entry.origSize)
			);
			if (got < 0) {
				TOAST_ERROR("LZ4 decompress failed");
				return false;
			}
			return true;
		}

		out.resize(static_cast<size_t>(entry.storedSize));
		std::copy(m_decompBuf.begin(), m_decompBuf.begin() + static_cast<ptrdiff_t>(entry.storedSize), out.begin());
		return true;
	}

	std::ifstream m_in;
	std::mutex m_readMtx;    // protects m_in during seek+read
	PackHeader m_header = {};
	std::vector<FileEntry> m_entries;
	std::unordered_map<std::u8string, HashMapEntry> m_lookupMap;
	std::vector<uint8_t> m_decompBuf;
};
}
