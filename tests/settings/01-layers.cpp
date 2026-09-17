#include "toast/settings/settings.hpp"

#include "test_registry.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

using namespace toast::settings;

namespace {

auto tempDir() -> std::filesystem::path {
	auto dir = std::filesystem::temp_directory_path() / "toast_settings_layers";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	return dir;
}

auto readAll(const std::filesystem::path& path) -> std::string {
	std::ifstream in(path);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}

/// The layering rule the whole design rests on: the editor authors project defaults, a player's own tweaks go
/// in the user layer on top, and a save writes only the layer being edited
TOAST_TEST_NAMED("settings", "settings/01-layers", test_settings_01_layers) {
	const auto dir = tempDir();
	const auto project_file = dir / "settings.toml";
	const auto user_file = dir / "user" / "settings.toml";

	auto& settings = Settings::get();
	settings.setPaths(project_file, user_file);

	auto quality = declareInt("test.layers.quality", 1, {.label = "Quality"});
	auto enabled = declareBool("test.layers.enabled", true);

	assert(quality.value() == 1);
	assert(enabled.value() == true);

	// Editor: authoring the defaults that ship
	settings.setActiveLayer(Layer::project);
	quality.set(3);
	assert(quality.value() == 3);
	assert(settings.save());
	assert(std::filesystem::exists(project_file));

	// A value left at its code default is not written, so a later patch may still move it
	const auto project_text = readAll(project_file);
	assert(project_text.find("quality") != std::string::npos);
	assert(project_text.find("enabled") == std::string::npos);

	// Packaged game: a player's tweak lands on top without touching what shipped
	settings.setActiveLayer(Layer::user);
	quality.set(0);
	assert(quality.value() == 0);
	assert(settings.save());
	assert(std::filesystem::exists(user_file));

	// The project file still says what the editor authored
	assert(readAll(project_file).find("quality") != std::string::npos);

	// Dropping the user override exposes the project value underneath rather than the code default
	assert(settings.reset("test.layers.quality"));
	assert(quality.value() == 3);

	// And dropping the project override falls the rest of the way to the code default
	settings.setActiveLayer(Layer::project);
	assert(settings.reset("test.layers.quality"));
	assert(quality.value() == 1);

	std::filesystem::remove_all(dir);
}
