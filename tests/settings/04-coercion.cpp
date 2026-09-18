#include "toast/settings/settings.hpp"

#include "test_registry.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

using namespace toast::settings;

namespace {

auto tempDir() -> std::filesystem::path {
	auto dir = std::filesystem::temp_directory_path() / "toast_settings_coercion";
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
/// TOML gives back whatever the file literally held, so an integer written for a float setting has to widen
/// rather than be rejected - a hand-edited file writing `1` instead of `1.0` is the normal case
TOAST_TEST_NAMED("settings", "settings/04-coercion", test_settings_04_coercion) {
	const auto dir = tempDir();
	const auto project_file = dir / "settings.toml";

	{
		std::ofstream out(project_file);
		out << "[test.coerce]\nratio = 2\n";
	}

	auto& settings = Settings::get();
	settings.setPaths(project_file, dir / "user" / "settings.toml");
	settings.setActiveLayer(Layer::project);
	settings.load();

	auto ratio = declareFloat("test.coerce.ratio", 0.0);
	assert(ratio.value() == 2.0);

	// A string against a number is a genuinely wrong file, not a formatting accident
	assert(!settings.setValue("test.coerce.ratio", Value {std::string {"nope"}}));
	assert(ratio.value() == 2.0);

	std::filesystem::remove_all(dir);
}
