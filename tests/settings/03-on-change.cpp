#include "toast/settings/settings.hpp"

#include "test_registry.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

using namespace toast::settings;

namespace {

auto tempDir() -> std::filesystem::path {
	auto dir = std::filesystem::temp_directory_path() / "toast_settings_on_change";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	return dir;
}

}

/// The layering rule the whole design rests on: the editor authors project defaults, a player's own tweaks go
/// in the user layer on top, and a save writes only the layer being edited
/// onChange is how a system applies a setting, so it has to fire once at declaration - otherwise a stored
/// value only takes effect after the user edits it again
TOAST_TEST_NAMED("settings", "settings/03-on-change", test_settings_03_on_change) {
	const auto dir = tempDir();
	auto& settings = Settings::get();
	settings.setPaths(dir / "settings.toml", dir / "user" / "settings.toml");
	settings.setActiveLayer(Layer::project);

	auto volume = declareFloat("test.change.volume", 0.5);

	int calls = 0;
	double last = -1.0;
	volume.onChange([&](double v) {
		++calls;
		last = v;
	});

	assert(calls == 1);
	assert(last == 0.5);

	volume.set(0.25);
	assert(calls == 2);
	assert(last == 0.25);

	// Setting the same value again is not a change, and re-applying it would have systems rebuild for nothing
	volume.set(0.25);
	assert(calls == 2);

	std::filesystem::remove_all(dir);
}
