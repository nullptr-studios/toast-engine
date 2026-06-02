#include "uri_handler.hpp"

#include "ffi/engine.h"

#include <filesystem>

namespace toast { }

extern "C" {

void uri_set_working_directory(const char* path_str) {
	auto path = std::filesystem::path(path_str);
	auto assets_path = path / "assets";
	auto artwork_path = path / "artwork";

	toast::setAssetsPath(assets_path.string());
	toast::setArtworkPath(artwork_path.string());
}
}
