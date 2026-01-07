#pragma once

#include "Toast/Log.hpp"
#include "Toast/Profiler.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <lz4.h>
#include <string>
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

class PackFile {
public:
	bool Open(const std::string& pack_path) {
		PROFILE_ZONE;

		m_in.open(pack_path, std::ios::binary);
		if (!m_in) {
			return false;
		}

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

		m_hashes.clear();
		m_paths.clear();
		m_offsets.clear();
		m_origSizes.clear();
		m_storedSizes.clear();
		m_flags.clear();
		m_hashes.reserve(file_count);
		m_paths.reserve(file_count);

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
				std::cerr << "Failed reading table entry " << i << "\n";
				return false;
			}

			m_hashes.emplace_back(hsh);
			m_paths.emplace_back(std::move(path));
			m_offsets.emplace_back(offset);
			m_origSizes.emplace_back(orig);
			m_storedSizes.emplace_back(stored);
			m_flags.emplace_back(f);
		}

		return true;
	}

	void Close() {
		if (m_in.is_open()) {
			m_in.close();
		}
		m_hashes.clear();
		m_paths.clear();
		m_offsets.clear();
		m_origSizes.clear();
		m_storedSizes.clear();
		m_flags.clear();
	}

	bool ReadFile(const std::string& raw_path, std::vector<uint8_t>& out) {
		PROFILE_ZONE;

		// canonicalize lookup path same as packer
		std::u8string path = canonical_path_for_pack(raw_path);

		uint64_t h = fnv1a_hash64(path);
		auto it = std::ranges::lower_bound(m_hashes, h);
		// not found quickly
		if (it == m_hashes.end() || *it != h) {
			throw ToastException("PackFile: Path not found");
			return false;
		}

		size_t idx = static_cast<size_t>(it - m_hashes.begin());
		// collision scan
		size_t lo = idx, hi = idx;
		while (lo > 0 && m_hashes[lo - 1] == h) {
			--lo;
		}
		while (hi + 1 < m_hashes.size() && m_hashes[hi + 1] == h) {
			++hi;
		}

		for (size_t i = lo; i <= hi; ++i) {
			if (m_paths[i] == path) {
				return read_at_index(i, out);
			}
		}
		throw ToastException("PackFile: Hash collision but path not found???");
		return false;
	}

private:
	bool read_at_index(size_t i, std::vector<uint8_t>& out) {
		PROFILE_ZONE;

		uint64_t offset = m_offsets[i];
		uint64_t stored = m_storedSizes[i];
		uint64_t orig = m_origSizes[i];
		uint8_t f = m_flags[i];

		std::vector<uint8_t> stored_buf(static_cast<size_t>(stored));
		m_in.seekg(static_cast<std::streamoff>(offset));
		m_in.read(reinterpret_cast<char*>(stored_buf.data()), static_cast<std::streamsize>(stored));
		if (!m_in) {
			TOAST_ERROR("Read error at offset  {0}", offset);
			return false;
		}

		if (f & 1) {
			out.resize(static_cast<size_t>(orig));
			int got = LZ4_decompress_safe(
			    reinterpret_cast<const char*>(stored_buf.data()), reinterpret_cast<char*>(out.data()), static_cast<int>(stored), static_cast<int>(orig)
			);
			if (got < 0) {
				TOAST_ERROR("LZ4 decompress failed");
				return false;
			}
			return true;
		}

		out.swap(stored_buf);
		return true;
	}

	std::ifstream m_in;
	PackHeader m_header = {};
	std::vector<uint64_t> m_hashes;
	std::vector<std::u8string> m_paths;
	std::vector<uint64_t> m_offsets;
	std::vector<uint64_t> m_origSizes;
	std::vector<uint64_t> m_storedSizes;
	std::vector<uint8_t> m_flags;
};
}
