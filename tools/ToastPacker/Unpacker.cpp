/// @file Unpacker.cpp
/// @author dario
/// @date 18/09/2025.

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <lz4.h>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct PackHeader {
	char magic[9];
	uint32_t version;
	uint32_t file_count;
	uint64_t file_table_offset;
};

// canonicalization helper — must match packer
static inline std::u8string canonical_path_for_pack(const fs::path& p) {
	fs::path normalized = p.lexically_normal();
	std::u8string s = normalized.u8string();
	std::replace(s.begin(), s.end(), '\\', '/');
	if (s.size() >= 2 && s[0] == '.' && s[1] == '/') { s.erase(0, 2); }
	return s;
}

uint64_t fnv1a_hash64(const std::u8string& s) {
	uint64_t hash = 14695981039346656037ULL;
	for (unsigned char c : s) {
		hash ^= static_cast<uint64_t>(c);
		hash *= 1099511628211ULL;
	}
	return hash;
}

class PackFile {
public:
	bool Open(const std::string& packPath) {
		in_.open(packPath, std::ios::binary);
		if (!in_) { return false; }

		PackHeader h;
		in_.read(reinterpret_cast<char*>(&h), sizeof(h));
		if (!in_) { return false; }

		if (memcmp(h.magic, "TOASTPACK", 9) != 0) { throw std::runtime_error("Invalid pack file magic"); }
		header_ = h;

		// safety: verify table offset looks inside file
		in_.seekg(0, std::ios::end);
		uint64_t fileSize = static_cast<uint64_t>(in_.tellg());
		if (header_.file_table_offset == 0 || header_.file_table_offset >= fileSize) { throw std::runtime_error("Invalid file_table_offset in header"); }

		// read table
		in_.seekg(static_cast<std::streamoff>(header_.file_table_offset));
		uint32_t file_count = 0;
		in_.read(reinterpret_cast<char*>(&file_count), sizeof(file_count));
		if (!in_) { return false; }

		hashes_.clear();
		paths_.clear();
		offsets_.clear();
		orig_sizes_.clear();
		stored_sizes_.clear();
		flags_.clear();
		hashes_.reserve(file_count);
		paths_.reserve(file_count);

		for (uint32_t i = 0; i < file_count; ++i) {
			uint64_t hsh;
			uint32_t path_len;
			in_.read(reinterpret_cast<char*>(&hsh), sizeof(hsh));
			in_.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));
			std::u8string path;
			path.resize(path_len);
			in_.read(reinterpret_cast<char*>(&path[0]), path_len);

			uint64_t offset, orig, stored;
			uint8_t f;
			in_.read(reinterpret_cast<char*>(&offset), sizeof(offset));
			in_.read(reinterpret_cast<char*>(&orig), sizeof(orig));
			in_.read(reinterpret_cast<char*>(&stored), sizeof(stored));
			in_.read(reinterpret_cast<char*>(&f), sizeof(f));

			if (!in_) {
				std::cerr << "Failed reading table entry " << i << "\n";
				return false;
			}

			hashes_.push_back(hsh);
			paths_.push_back(std::move(path));
			offsets_.push_back(offset);
			orig_sizes_.push_back(orig);
			stored_sizes_.push_back(stored);
			flags_.push_back(f);
		}

		return true;
	}

	void Close() {
		if (in_.is_open()) { in_.close(); }
		hashes_.clear();
		paths_.clear();
		offsets_.clear();
		orig_sizes_.clear();
		stored_sizes_.clear();
		flags_.clear();
	}

	bool ReadFile(const std::string& raw_path, std::vector<uint8_t>& out) {
		// canonicalize lookup path same as packer
		std::u8string path = canonical_path_for_pack(raw_path);

		uint64_t h = fnv1a_hash64(path);
		auto it = std::lower_bound(hashes_.begin(), hashes_.end(), h);
		// not found quickly
		if (it == hashes_.end() || *it != h) {
			// fallback: linear scan to help debugging/compatibility
			for (size_t i = 0; i < paths_.size(); ++i) {
				if (paths_[i] == path) {    // found by exact string despite hash mismatch
					return read_at_index(i, out);
				}
			}
			return false;
		}

		size_t idx = static_cast<size_t>(it - hashes_.begin());
		// collision scan
		size_t lo = idx, hi = idx;
		while (lo > 0 && hashes_[lo - 1] == h) {
			--lo;
		}
		while (hi + 1 < hashes_.size() && hashes_[hi + 1] == h) {
			++hi;
		}

		for (size_t i = lo; i <= hi; ++i) {
			if (paths_[i] == path) { return read_at_index(i, out); }
		}
		return false;
	}

private:
	bool read_at_index(size_t i, std::vector<uint8_t>& out) {
		uint64_t offset = offsets_[i];
		uint64_t stored = stored_sizes_[i];
		uint64_t orig = orig_sizes_[i];
		uint8_t f = flags_[i];

		std::vector<uint8_t> stored_buf(static_cast<size_t>(stored));
		in_.seekg(static_cast<std::streamoff>(offset));
		in_.read(reinterpret_cast<char*>(stored_buf.data()), static_cast<std::streamsize>(stored));
		if (!in_) {
			std::cerr << "Read error at offset " << offset << "\n";
			return false;
		}

		if (f & 1) {
			out.resize(static_cast<size_t>(orig));
			int got = LZ4_decompress_safe(
			    reinterpret_cast<const char*>(stored_buf.data()), reinterpret_cast<char*>(out.data()), static_cast<int>(stored), static_cast<int>(orig)
			);
			if (got < 0) {
				std::cerr << "LZ4 decompress failed\n";
				return false;
			}
			return true;
		}

		out.swap(stored_buf);
		return true;
	}

	std::ifstream in_;
	PackHeader header_ = {};
	std::vector<uint64_t> hashes_;
	std::vector<std::u8string> paths_;
	std::vector<uint64_t> offsets_;
	std::vector<uint64_t> orig_sizes_;
	std::vector<uint64_t> stored_sizes_;
	std::vector<uint8_t> flags_;
};

int main(int argc, char** argv) {
	if (argc < 3) {
		std::cout << "Usage: pack_loader <pack_file> <file_in_pack>\n";
		return 1;
	}

	std::string pack = argv[1];
	std::string target = argv[2];

	PackFile pf;
	if (!pf.Open(pack)) {
		std::cerr << "Failed to open pack: " << pack << "\n";
		return 1;
	}

	std::vector<uint8_t> data;
	if (!pf.ReadFile(target, data)) {
		std::cerr << "File not found in pack: " << target << "\n";
		return 1;
	}

	std::cout << "Read " << data.size() << " bytes for " << target << "\n";

	// Write it back to disk
	std::ofstream fout("extracted_" + std::filesystem::path(target).filename().string(), std::ios::binary);
	fout.write(reinterpret_cast<const char*>(data.data()), data.size());

	fout.close();
	std::cout << "Wrote extracted file to disk.\n";
	pf.Close();

	return 0;
}
