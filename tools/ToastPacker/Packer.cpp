/// @file Packer.cpp
/// @author Dario
/// @date 18/09/25

/*
 *  [ Header (fixed-size) ]                -- sizeof(PackHeader)
 *  [ File data block 1 ]                  -- raw bytes (file A)
 *  [ File data block 2 ]                  -- raw bytes (file B)
 *  ...
 *  [ File data block N ]                  -- raw bytes (file N)
 *  [ File table offset position (written in header) points here ]
 *  [ uint32_t file_count ]
 *  For each file:
 *  [ uint32_t path_len ]
 *  [ path_len bytes (utf-8, forward slashes) ]
 *  [ uint64_t offset ]   -- offset where this file's data begins
 *  [ uint64_t size ]     -- length in bytes
 */

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <lz4.h>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Header
struct PackHeader {
	char magic[9];
	uint32_t version;
	uint32_t file_count;
	uint64_t file_table_offset;
};

static inline std::u8string canonical_path_for_pack(const fs::path& p, const fs::path& base) {
	fs::path rel;
	try {
		rel = fs::relative(p, base);
	} catch (...) { rel = p.lexically_normal(); }
	rel = rel.lexically_normal();
	std::u8string s = rel.u8string();               // UTF-8 bytes
	std::replace(s.begin(), s.end(), '\\', '/');    // always use forward slashes
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

int main(int argc, char** argv) {
	if (argc < 3) {
		std::cerr << "Usage: asset_packer <assets_folder> <out.pack>\n";
		return 1;
	}

	fs::path assets_root = argv[1];
	fs::path out_pack = argv[2];
	if (!fs::is_directory(assets_root)) {
		std::cerr << "Not a directory: " << assets_root << "\n";
		return 1;
	}

	std::vector<fs::path> files;
	for (auto& it : fs::recursive_directory_iterator(assets_root)) {
		if (it.is_regular_file()) { files.push_back(it.path()); }
	}

	std::ofstream out(out_pack, std::ios::binary);
	if (!out) {
		std::cerr << "Failed to create pack: " << out_pack << "\n";
		return 1;
	}

	PackHeader hdr {};
	std::memcpy(hdr.magic, "TOASTPACK", 9);
	hdr.version = 2;
	hdr.file_count = 0;
	hdr.file_table_offset = 0;
	out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

	struct TempEntry {
		std::u8string rel;
		uint64_t hash;
		uint64_t offset;
		uint64_t orig_size;
		uint64_t stored_size;
		uint8_t flags;
	};

	std::vector<TempEntry> entries;
	uint64_t count = 0;
	for (auto& p : files) {
		// read file on disk
		std::ifstream in(p, std::ios::binary);
		if (!in) {
			std::cerr << "Failed to open " << p << "\n";
			return 1;
		}

		// robust file size using filesystem
		uint64_t size = static_cast<uint64_t>(fs::file_size(p));

		// read whole file (keeps logic identical to your original)
		std::vector<char> buf(size ? static_cast<size_t>(size) : 0);
		if (size) { in.read(buf.data(), static_cast<std::streamsize>(size)); }

		// prepare payload buffer and attempt LZ4 compression
		std::vector<char> payload;
		int maxC = size ? LZ4_compressBound(static_cast<int>(size)) : 0;
		if (maxC > 0) {
			payload.resize(static_cast<size_t>(maxC));
			int csize = LZ4_compress_default(buf.data(), payload.data(), static_cast<int>(size), maxC);
			if (csize > 0 && static_cast<uint64_t>(csize) + 8 < size) {
				payload.resize(static_cast<size_t>(csize));    // keep compressed
			} else {
				payload.assign(buf.begin(), buf.end());        // not beneficial -> raw
			}
		} else {
			// empty file
			payload.clear();
		}

		// IMPORTANT: record offset BEFORE writing payload
		uint64_t offset = static_cast<uint64_t>(out.tellp());

		if (!payload.empty()) {
			out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
			if (!out) {
				std::cerr << "Write error while writing payload for " << p << "\n";
				return 1;
			}
		}

		uint8_t flags = 0;
		if (payload.size() < size) {
			flags |= 1;    // compressed
		}

		std::u8string rel = canonical_path_for_pack(p, assets_root);
		entries.push_back({ rel, fnv1a_hash64(rel), offset, size, payload.size(), flags });

		std::string file(rel.begin(), rel.end());
		// print info
		std::cout << "Packed: " << file << " (" << size << " bytes";
		if (flags) {
			std::cout << " -> " << payload.size() << " bytes COMPRESSED  -" << (size - payload.size()) << " bytes)\n";
		} else {
			std::cout << " RAW)\n";
		}
		count++;
		std::cout << "\rFiles processed: " << count << "/" << files.size() << "   " << std::flush;
	}

	// sort entries by hash
	std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) {
		if (a.hash != b.hash) { return a.hash < b.hash; }
		return a.rel < b.rel;
	});

	// Write table: record table_offset BEFORE writing the table content
	uint64_t table_offset = static_cast<uint64_t>(out.tellp());
	uint32_t file_count = static_cast<uint32_t>(entries.size());
	out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));
	for (auto& e : entries) {
		out.write(reinterpret_cast<const char*>(&e.hash), sizeof(e.hash));
		uint32_t path_len = static_cast<uint32_t>(e.rel.size());
		out.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
		out.write(reinterpret_cast<const char*>(e.rel.data()), path_len);
		out.write(reinterpret_cast<const char*>(&e.offset), sizeof(e.offset));
		out.write(reinterpret_cast<const char*>(&e.orig_size), sizeof(e.orig_size));
		out.write(reinterpret_cast<const char*>(&e.stored_size), sizeof(e.stored_size));
		out.write(reinterpret_cast<const char*>(&e.flags), sizeof(e.flags));
	}

	// finalize header: set file_count and table_offset then rewrite header at file start
	hdr.file_count = file_count;
	hdr.file_table_offset = table_offset;
	out.seekp(0);
	out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
	out.close();

	std::cout << "\nWrote pack: " << out_pack << " (" << file_count << " files)\n\n";

	uint64_t TotalOriginal = 0, TotalStored = 0;
	for (auto& e : entries) {
		TotalOriginal += e.orig_size;
		TotalStored += e.stored_size;
	}
	std::cout << "Total Original Size: " << TotalOriginal << " bytes\n";
	std::cout << "Total Stored Size:   " << TotalStored << " bytes\n";
	std::cout << "Overall Compression: " << (TotalOriginal ? (100.0 * (TotalOriginal - TotalStored) / TotalOriginal) : 0.0) << " %\n";
	return 0;
}
