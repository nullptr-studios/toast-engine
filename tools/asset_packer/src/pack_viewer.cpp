/**
 * @file pack_viewer.cpp
 * @author Xein
 * @date 7 July 2026
 *
 * Validates a .pak archive and prints its contents as a tree
 *
 * Validation checks:
 *   - magic and version in header
 *   - file_table_offset is within the file
 *   - every entry's offset + stored_size <= file_table_offset
 *   - path_len is sane (<= 4096)
 *   - for each compressed entry: decompresses cleanly to exactly orig_size bytes
 *
 * Usage: pack_viewer <pack>
 */

#include "console.hpp"

#include <toast_pack/pack.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TreeNode {
	std::map<std::string, TreeNode> children;
	const pack::Entry* file = nullptr;    // null for directories
};

void insertEntry(TreeNode& root, const pack::Entry& entry) {
	TreeNode* node = &root;
	std::size_t start = 0;
	while (true) {
		const std::size_t slash = entry.rel.find('/', start);
		if (slash == std::string::npos) {
			node->children[entry.rel.substr(start)].file = &entry;
			return;
		}
		node = &node->children[entry.rel.substr(start, slash - start)];
		start = slash + 1;
	}
}

void printNode(const TreeNode& node, const std::string& prefix) {
	std::size_t i = 0;
	for (const auto& [name, child] : node.children) {
		const bool is_last = ++i == node.children.size();
		const char* connector = is_last ? "└── " : "├── ";
		const char* child_prefix = is_last ? "    " : "│   ";

		if (child.file) {
			const pack::Entry& e = *child.file;
			std::string info;
			if (e.isCompressed()) {
				const double pct = 100.0 * static_cast<double>(e.orig_size - e.stored_size) / static_cast<double>(std::max<uint64_t>(e.orig_size, 1));
				info = std::format("{} bytes → {} stored  [{:.1f}% compression]", e.orig_size, e.stored_size, pct);
			} else {
				info = std::format("{} bytes  [RAW]", e.orig_size);
			}
			std::cout << prefix << connector << name << "  (" << info << ")\n";
		} else {
			std::cout << prefix << connector << name << "/\n";
			printNode(child, prefix + child_prefix);
		}
	}
}

int run(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "Validate and display the contents of a .pak file\n\nUsage: pack_viewer <pack>\n";
		return 1;
	}

	std::error_code ec;
	const fs::path pack_path = fs::canonical(argv[1], ec);
	if (ec) {
		throw std::runtime_error(std::format("cannot access '{}': {}", argv[1], ec.message()));
	}

	std::ifstream r(pack_path, std::ios::binary);
	if (!r) {
		throw std::runtime_error(std::format("cannot open '{}'", pack_path.string()));
	}
	const uint64_t file_len = fs::file_size(pack_path);

	std::cout << "Pack: " << pack_path.string() << '\n';
	std::cout << "Size: " << file_len << " bytes\n\n";

	pack::Header header;
	try {
		header = pack::readHeader(r);
	} catch (const std::exception& e) {
		throw std::runtime_error(std::format("Header validation failed: {}", e.what()));
	}
	std::cout << "Header:\n";
	std::cout << "  magic:             PACK\\0\\0\n";
	std::cout << "  version:           " << pack::VERSION << '\n';
	std::cout << "  file_count:        " << header.file_count << '\n';
	std::cout << "  file_table_offset: " << header.file_table_offset << '\n';

	if (header.file_table_offset >= file_len) {
		throw std::runtime_error(std::format(
		    "Validation FAILED: file_table_offset {} is beyond file length {}", header.file_table_offset, file_len
		));
	}

	r.seekg(static_cast<std::streamoff>(header.file_table_offset));
	std::vector<pack::Entry> entries;
	try {
		entries = pack::readTable(r, header.file_count);
	} catch (const std::exception& e) {
		throw std::runtime_error(std::format("File-table validation failed: {}", e.what()));
	}

	uint64_t total_orig = 0;
	uint64_t total_stored = 0;
	std::vector<std::string> errors;

	for (const auto& e : entries) {
		// data block must lie entirely between the header and the table
		const uint64_t end = e.offset + e.stored_size < e.offset ? UINT64_MAX : e.offset + e.stored_size;
		if (e.offset < pack::HEADER_SIZE) {
			errors.push_back(std::format("'{}': offset {} is before end of header ({})", e.rel, e.offset, pack::HEADER_SIZE));
		} else if (end > header.file_table_offset) {
			errors.push_back(std::format(
			    "'{}': data block [{}, {}) extends past file_table_offset {}", e.rel, e.offset, end, header.file_table_offset
			));
		}

		// decompress test for compressed entries
		if (e.isCompressed() && errors.empty()) {
			try {
				const auto blob = pack::readBlobAt(r, e.offset, e.stored_size);
				const auto data = pack::decompress(blob, e.orig_size);
				if (data.size() != e.orig_size) {
					errors.push_back(std::format("'{}': decompressed to {} bytes, expected {}", e.rel, data.size(), e.orig_size));
				}
			} catch (const std::exception& err) {
				errors.push_back(std::format("'{}': {}", e.rel, err.what()));
			}
		}

		total_orig += e.orig_size;
		total_stored += e.stored_size;
	}

	if (!errors.empty()) {
		std::cout << "\nValidation FAILED:\n";
		for (const auto& err : errors) {
			std::cout << "  ✗ " << err << '\n';
		}
		return 1;
	}

	std::cout << "\nValidation: OK\n\n";

	TreeNode root;
	for (const auto& e : entries) {
		insertEntry(root, e);
	}
	printNode(root, "");

	const double pct = total_orig > 0 ? 100.0 * static_cast<double>(total_orig - total_stored) / static_cast<double>(total_orig) : 0.0;
	constexpr double mib = 1048576.0;
	std::cout << '\n' << entries.size() << " file(s)\n";
	std::cout << std::format("Original size: {:.3f} MB\n", static_cast<double>(total_orig) / mib);
	std::cout << std::format("Stored size:   {:.3f} MB\n", static_cast<double>(total_stored) / mib);
	std::cout << std::format("Compression:   {:.1f} %\n", pct);
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
