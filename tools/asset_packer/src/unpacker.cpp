/**
 * @file unpacker.cpp
 * @author Xein
 * @date 7 Jul 2026
 *
 * Extracts a .pak archive back to the filesystem
 *
 * Usage: unpacker <pack> [output_dir]
 *   <pack>        Path to the .pak file
 *   [output_dir]  Output directory (default: ./<pack_stem>/)
 */

#include "console.hpp"

#include <toast_pack/pack.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

int run(int argc, char** argv) {
	if (argc < 2 || argc > 3) {
		std::cerr << "Extract a .pak archive to a directory\n\nUsage: unpacker <pack> [output_dir]\n"
		          << "  <pack>        Path to the .pak file\n"
		          << "  [output_dir]  Output directory (default: ./<pack_stem>/)\n";
		return 1;
	}

	std::error_code ec;
	const fs::path pack_path = fs::canonical(argv[1], ec);
	if (ec) {
		throw std::runtime_error(std::format("cannot access '{}': {}", argv[1], ec.message()));
	}

	const std::string stem = pack_path.stem().string();
	if (stem.empty()) {
		throw std::runtime_error("pack file has no stem");
	}
	const fs::path out_dir = argc == 3 ? fs::absolute(argv[2]) : fs::current_path() / stem;

	std::cout << std::format("Extracting '{}' → '{}'\n", pack_path.string(), out_dir.string());

	const pack::PackReader reader(pack_path);
	const auto entries = reader.entries();
	std::cout.flush();
	if (entries.empty()) {
		std::cout << "Pack is empty, nothing to extract.\n";
		return 0;
	}

	fs::create_directories(out_dir);

	console::Progress progress(entries.size());
	std::size_t done = 0;
	for (const auto& entry : entries) {
		progress.update(done, entry.rel);

		const auto data = reader.readEntry(entry);

		const fs::path dest = out_dir / fs::path(std::u8string(entry.rel.begin(), entry.rel.end()));
		if (dest.has_parent_path()) {
			fs::create_directories(dest.parent_path());
		}
		std::ofstream out(dest, std::ios::binary | std::ios::trunc);
		if (!out) {
			throw std::runtime_error(std::format("cannot create '{}'", dest.string()));
		}
		out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
		if (!out) {
			throw std::runtime_error(std::format("write to '{}' failed", dest.string()));
		}

		progress.update(++done, entry.rel);
	}
	progress.finish();

	std::cout << std::format("Extracted {} file(s) to '{}'\n", entries.size(), out_dir.string());
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
