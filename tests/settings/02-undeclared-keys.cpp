#include "toast/settings/settings.hpp"

#include "test_registry.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

using namespace toast::settings;

namespace {

auto tempDir() -> std::filesystem::path {
	auto dir = std::filesystem::temp_directory_path() / "toast_settings_undeclared";
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
/// A settings file outlives the code that reads it: a key may belong to a system not constructed yet, and it
/// has to survive both the load that could not type it and the save that never saw it declared
TOAST_TEST_NAMED("settings", "settings/02-undeclared-keys", test_settings_02_undeclared) {
	const auto dir = tempDir();
	const auto project_file = dir / "settings.toml";
	const auto user_file = dir / "user" / "settings.toml";

	{
		std::ofstream out(project_file);
		out << "[test.pending]\nlate = 7\nforeign = \"kept\"\n";
	}

	auto& settings = Settings::get();
	settings.setPaths(project_file, user_file);
	settings.setActiveLayer(Layer::project);
	settings.load();

	// Declared after the file was read: the stored value still reaches it
	auto late = declareInt("test.pending.late", 1);
	assert(late.value() == 7);

	// Nothing declares this one, and a save must not prune it
	assert(settings.save());
	assert(readAll(project_file).find("foreign") != std::string::npos);

	std::filesystem::remove_all(dir);
}
