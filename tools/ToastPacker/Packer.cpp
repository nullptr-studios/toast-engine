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
 *  [ uint64_t hash ]
 *  [ uint32_t path_len ]
 *  [ path_len bytes (utf-8, forward slashes) ]
 *  [ uint64_t offset ]   -- offset where this file's data begins
 *  [ uint64_t orig_size ]     -- original length in bytes
 *  [ uint64_t stored_size ]   -- stored length in bytes
 *  [ uint8_t flags ]     -- compression flags
 */

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <lz4.h>
#include <string>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace fs = std::filesystem;

// Header
struct PackHeader {
	char magic[9];
	uint32_t version;
	uint32_t file_count;
	uint64_t file_table_offset;
};

struct CompressionWork {
	uint64_t file_index;
	std::string path;
	std::vector<char> buffer;
	uint64_t size;
};

struct CompressionResult {
	uint64_t file_index;
	std::u8string rel;
	uint64_t hash;
	uint64_t offset;
	uint64_t orig_size;
	uint64_t stored_size;
	uint8_t flags;
	std::vector<char> payload;
};

static inline bool should_compress(const std::string& path) {
	static const std::vector<std::string> skip_extensions = {
		".jpg", ".jpeg", ".png", ".webp",  // images already compressed
		".mp3", ".ogg", ".wav", ".flac",   // audio already compressed
		".zip", ".7z", ".rar",             // archives already compressed
		".mp4", ".webm", ".avi"            // video already compressed
	};
	std::string ext = fs::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	return std::find(skip_extensions.begin(), skip_extensions.end(), ext) == skip_extensions.end();
}

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

static CompressionResult compress_file(uint64_t file_index, const fs::path& p, const fs::path& assets_root, std::vector<char>& buf, uint64_t size) {
	CompressionResult result;
	result.file_index = file_index;
	result.rel = canonical_path_for_pack(p, assets_root);
	result.hash = fnv1a_hash64(result.rel);
	result.orig_size = size;
	result.flags = 0;

	// Check if file type should be compressed
	if (!should_compress(p.string()) || size == 0) {
		// Skip compression for already-compressed or empty files
		result.payload = std::move(buf);
		result.stored_size = result.orig_size;
		return result;
	}

	// Attempt LZ4 compression
	int maxC = LZ4_compressBound(static_cast<int>(size));
	if (maxC > 0) {
		result.payload.resize(static_cast<size_t>(maxC));
		int csize = LZ4_compress_default(buf.data(), result.payload.data(), static_cast<int>(size), maxC);
		
		if (csize > 0 && static_cast<uint64_t>(csize) < size) {
			// Compression beneficial
			result.payload.resize(static_cast<size_t>(csize));
			result.stored_size = static_cast<uint64_t>(csize);
			result.flags |= 1;  // compressed
		} else {
			// keep raw
			result.payload = std::move(buf);
			result.stored_size = result.orig_size;
		}
	} else {
		result.payload = std::move(buf);
		result.stored_size = result.orig_size;
	}

	return result;
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

	// Collect all files
	std::vector<fs::path> files;
	for (auto& it : fs::recursive_directory_iterator(assets_root)) {
		if (it.is_regular_file()) { files.push_back(it.path()); }
	}

	std::cout << "Collected " << files.size() << " files, starting compression...\n";

	// Open output with buffering
	std::ofstream out(out_pack, std::ios::binary);
	if (!out) {
		std::cerr << "Failed to create pack: " << out_pack << "\n";
		return 1;
	}
	out.rdbuf()->pubsetbuf(nullptr, 65536);  // 64KB buffer

	// Write placeholder header
	PackHeader hdr {};
	std::memcpy(hdr.magic, "TOASTPACK", 9);
	hdr.version = 2;
	hdr.file_count = 0;
	hdr.file_table_offset = 0;
	out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

	std::vector<CompressionResult> results;
	results.reserve(files.size());

	const size_t num_threads = std::max(1u, std::thread::hardware_concurrency());
	std::vector<std::thread> threads;
	std::queue<CompressionWork> work_queue;
	std::mutex queue_mutex, result_mutex;
	std::condition_variable cv;
	bool done_reading = false;

	// Worker thread
	auto worker = [&]() {
		while (true) {
			CompressionWork work;
			{
				std::unique_lock lock(queue_mutex);
				cv.wait(lock, [&] { return !work_queue.empty() || done_reading; });
				if (work_queue.empty()) break;
				work = std::move(work_queue.front());
				work_queue.pop();
			}

			auto result = compress_file(work.file_index, work.path, assets_root, work.buffer, work.size);
			{
				std::lock_guard lock(result_mutex);
				results.push_back(std::move(result));
			}
		}
	};

	// Start worker threads
	for (size_t i = 0; i < num_threads; ++i) {
		threads.emplace_back(worker);
	}

	// Queue work items
	for (uint64_t i = 0; i < files.size(); ++i) {
		auto& p = files[i];
		std::ifstream in(p, std::ios::binary);
		if (!in) {
			std::cerr << "Failed to open " << p << "\n";
			return 1;
		}

		uint64_t size = static_cast<uint64_t>(fs::file_size(p));
		std::vector<char> buf(size ? static_cast<size_t>(size) : 0);
		if (size) { in.read(buf.data(), static_cast<std::streamsize>(size)); }

		CompressionWork work;
		work.file_index = i;
		work.path = p.string();
		work.buffer = std::move(buf);
		work.size = size;

		{
			std::unique_lock lock(queue_mutex);
			work_queue.push(std::move(work));
		}
		cv.notify_one();
	}

	{
		std::unique_lock lock(queue_mutex);
		done_reading = true;
	}
	cv.notify_all();

	// Wait for all threads
	for (auto& t : threads) {
		t.join();
	}

	std::cout << "Compression complete, writing pack...\n";

	std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
		if (a.hash != b.hash) return a.hash < b.hash;
		return a.rel < b.rel;
	});

	for (auto& result : results) {
		result.offset = static_cast<uint64_t>(out.tellp());
		if (!result.payload.empty()) {
			out.write(result.payload.data(), static_cast<std::streamsize>(result.payload.size()));
			if (!out) {
				std::cerr << "Write error\n";
				return 1;
			}
		}

		std::string file(result.rel.begin(), result.rel.end());
		std::cout << "Packed: " << file << " (" << result.orig_size << " bytes";
		if (result.flags & 1) {
			std::cout << " -> " << result.stored_size << " bytes COMPRESSED  -" << (result.orig_size - result.stored_size) << " bytes)\n";
		} else {
			std::cout << " RAW)\n";
		}
	}

	// Write file table
	uint64_t table_offset = static_cast<uint64_t>(out.tellp());
	uint32_t file_count = static_cast<uint32_t>(results.size());
	out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));
	
	for (const auto& e : results) {
		out.write(reinterpret_cast<const char*>(&e.hash), sizeof(e.hash));
		uint32_t path_len = static_cast<uint32_t>(e.rel.size());
		out.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
		out.write(reinterpret_cast<const char*>(e.rel.data()), path_len);
		out.write(reinterpret_cast<const char*>(&e.offset), sizeof(e.offset));
		out.write(reinterpret_cast<const char*>(&e.orig_size), sizeof(e.orig_size));
		out.write(reinterpret_cast<const char*>(&e.stored_size), sizeof(e.stored_size));
		out.write(reinterpret_cast<const char*>(&e.flags), sizeof(e.flags));
	}

	// Update header
	hdr.file_count = file_count;
	hdr.file_table_offset = table_offset;
	out.seekp(0);
	out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
	out.close();

	std::cout << "\nWrote pack: " << out_pack << " (" << file_count << " files)\n";

	uint64_t TotalOriginal = 0, TotalStored = 0;
	for (const auto& e : results) {
		TotalOriginal += e.orig_size;
		TotalStored += e.stored_size;
	}
	std::cout << "Total Original Size: " << TotalOriginal << " bytes\n";
	std::cout << "Total Stored Size:   " << TotalStored << " bytes\n";
	std::cout << "Overall Compression: " << (TotalOriginal ? (100.0 * (TotalOriginal - TotalStored) / TotalOriginal) : 0.0) << " %\n";
	return 0;
}
