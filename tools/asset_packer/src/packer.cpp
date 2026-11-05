/**
 * @file packer.cpp
 * @author Xein
 * @date 7 Jul 2026
 *
 * Packs a folder of files into a .pak archive
 *
 * Usage: packer <folder> [output]
 *   <folder>   Source directory
 *   [output]   Output file path (default: ./<folder_name>.pak)
 */

#include "console.hpp"

#include <toast_pack/pack.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

auto readFile(const fs::path& path) -> std::vector<uint8_t> {
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) {
		throw std::runtime_error(std::format("cannot read '{}'", path.string()));
	}
	const auto size = static_cast<std::size_t>(f.tellg());
	f.seekg(0);
	std::vector<uint8_t> data(size);
	if (size > 0 && !f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
		throw std::runtime_error(std::format("cannot read '{}'", path.string()));
	}
	return data;
}

int run(int argc, char** argv) {
	if (argc < 2 || argc > 3) {
		std::cerr << "Pack a folder into a .pak archive\n\nUsage: packer <folder> [output]\n"
		          << "  <folder>   Source directory\n"
		          << "  [output]   Output file path (default: ./<folder_name>.pak)\n";
		return 1;
	}

	std::error_code ec;
	const fs::path folder = fs::canonical(argv[1], ec);
	if (ec) {
		throw std::runtime_error(std::format("cannot access folder '{}': {}", argv[1], ec.message()));
	}
	if (!fs::is_directory(folder)) {
		throw std::runtime_error(std::format("'{}' is not a directory", folder.string()));
	}

	const std::string folder_name = folder.filename().string();
	if (folder_name.empty()) {
		throw std::runtime_error("folder has no file name component");
	}

	fs::path out_path;
	if (argc == 3) {
		out_path = fs::absolute(argv[2]);
		if (!out_path.has_extension()) {
			out_path.replace_extension("pak");
		}
	} else {
		out_path = fs::current_path() / (folder_name + ".pak");
	}
	if (out_path.has_parent_path()) {
		fs::create_directories(out_path.parent_path());
	}

	std::cout << std::format("Packing '{}' → '{}'\n", folder.string(), out_path.string());

	const auto files = pack::collectFiles(folder);
	if (files.empty()) {
		std::cout << "No files found, nothing to pack.\n";
		return 0;
	}

	std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
	if (!out) {
		throw std::runtime_error(std::format("cannot create '{}'", out_path.string()));
	}

	// placeholder header, rewritten once the table offset is known
	pack::writeHeader(out, {});

	std::vector<pack::Entry> entries;
	entries.reserve(files.size());
	uint64_t total_orig = 0;
	uint64_t total_stored = 0;

	console::Progress progress(files.size());
	std::size_t done = 0;
	for (const auto& path : files) {
		auto rel = pack::canonicalRel(path, folder);
		progress.update(done, rel);

		auto data = readFile(path);
		const uint64_t orig_size = data.size();

		auto compressed = pack::tryCompress(data);
		const bool is_compressed = compressed.has_value();
		const std::vector<uint8_t>& payload = is_compressed ? *compressed : data;

		const auto offset = static_cast<uint64_t>(out.tellp());
		if (!payload.empty()) {
			out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
			if (!out) {
				throw std::runtime_error(std::format("write to '{}' failed", out_path.string()));
			}
		}

		pack::Entry e;
		e.hash = pack::fnv1a64(rel);
		e.rel = std::move(rel);
		e.offset = offset;
		e.orig_size = orig_size;
		e.stored_size = payload.size();
		e.flags = is_compressed ? pack::FLAG_COMPRESSED : 0;
		total_orig += e.orig_size;
		total_stored += e.stored_size;
		entries.push_back(std::move(e));

		progress.update(++done, entries.back().rel);
	}
	progress.finish();

	// sort by (hash, rel) and write file table
	std::ranges::sort(entries, [](const pack::Entry& a, const pack::Entry& b) {
		return a.hash != b.hash ? a.hash < b.hash : a.rel < b.rel;
	});
	const auto table_offset = static_cast<uint64_t>(out.tellp());
	pack::writeTable(out, entries);

	// rewrite header with final values
	out.seekp(0);
	pack::writeHeader(out, {static_cast<uint32_t>(entries.size()), table_offset});
	out.flush();
	if (!out) {
		throw std::runtime_error(std::format("write to '{}' failed", out_path.string()));
	}

	const double pct = total_orig > 0 ? 100.0 * static_cast<double>(total_orig - total_stored) / static_cast<double>(total_orig) : 0.0;
	constexpr double mib = 1048576.0;
	std::cout << std::format("\nWrote '{}' ({} files)\n", out_path.string(), entries.size());
	std::cout << std::format("  Original size:  {:.3f} MiB\n", static_cast<double>(total_orig) / mib);
	std::cout << std::format("  Stored size:    {:.3f} MiB\n", static_cast<double>(total_stored) / mib);
	std::cout << std::format("  Compression:    {:.1f} %\n", pct);
	return 0;
}

}

int main(int argc, char** argv) {
	console::initUtf8();
	try {
		return run(argc, argv);
	} catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << '\n';
		return 1;
	}
}
