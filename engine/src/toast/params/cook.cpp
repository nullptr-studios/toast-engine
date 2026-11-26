#include "cook.hpp"

#include "document.hpp"

#include <fstream>
#include <sstream>
#include <toast/log.hpp>
#include <utility>
#include <vector>

namespace toast::params {
namespace {

auto readAll(const std::filesystem::path& path) -> std::string {
	std::ifstream in(path, std::ios::binary);
	std::stringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

}

auto cookConfigs(
    const std::filesystem::path& config_dir, const std::filesystem::path& toast_file, const std::filesystem::path& stage_dir
) -> bool {
	namespace fs = std::filesystem;
	std::error_code ec;
	const bool has_config = fs::is_directory(config_dir, ec);
	const bool has_toast = fs::is_regular_file(toast_file, ec);
	if (!has_config && !has_toast) {
		TOAST_ERROR("Params", "Nothing to cook: no {} and no {}", config_dir.string(), toast_file.string());
		return false;
	}

	std::vector<std::pair<std::string, Document>> outputs;
	const auto convert = [&](const fs::path& from, std::string name) {
		auto parsed = tomlToDocument(readAll(from));
		if (!parsed) {
			TOAST_ERROR("Params", "Cannot cook {}: {}", from.string(), parsed.error());
			return false;
		}
		outputs.emplace_back(std::move(name), std::move(*parsed));
		return true;
	};

	if (has_config) {
		std::vector<fs::path> files;
		for (const auto& entry : fs::directory_iterator(config_dir, ec)) {
			if (entry.is_regular_file() && entry.path().extension() == ".toml") {
				files.push_back(entry.path());
			}
		}
		std::ranges::sort(files);
		for (const auto& file : files) {
			if (!convert(file, file.stem().string() + ".bson")) {
				return false;
			}
		}
	}
	if (has_toast && !convert(toast_file, "project.toast.bson")) {
		return false;
	}

	fs::create_directories(stage_dir, ec);
	for (const auto& [name, document] : outputs) {
		const auto bytes = Document::to_bson(document);
		std::ofstream out(stage_dir / name, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!out.good()) {
			TOAST_ERROR("Params", "Cannot write {}", (stage_dir / name).string());
			return false;
		}
	}
	return true;
}

}
