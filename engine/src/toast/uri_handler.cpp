#include "uri_handler.hpp"

#include "ffi/engine.h"

#include <filesystem>

namespace toast { }

extern "C" {

void uri_set_working_directory(const char* project, const char* engine) {
	auto path = std::filesystem::path(project);
	auto engine_path = std::filesystem::path(engine);

	toast::setAssetsPath((path / "assets").string());
	toast::setArtworkPath((path / "artwork").string());
	toast::setCachePath((path / ".toast").string());
	toast::setCorePath((engine_path / "assets").string());
}
}
