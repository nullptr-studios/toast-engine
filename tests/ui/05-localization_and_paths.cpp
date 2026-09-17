#include "test_registry.hpp"

#include <cassert>
#include <string>
#include <toast/ui/assets/localization.hpp>
#include <toast/ui/ui_system_interface.hpp>
#include <vector>

namespace {

auto bytes(std::string_view text) -> std::vector<uint8_t> {
	return {text.begin(), text.end()};
}

}

TOAST_TEST_NAMED("ui", "ui/05-localization_and_paths", test_ui_05_localization_and_paths) {
	assets::Localization text(bytes("id,en,es\nhello,Hello,Hola\nfallback,Default,\n"));
	assert(text.languages() == std::vector<std::string>({"en", "es"}));
	assert(text.text("hello", "es") == "Hola");
	assert(text.text("fallback", "es") == "Default");
	assert(text.text("missing", "es") == "missing");

	auto* stable_address = &text;
	text.reload(bytes("id,es,en\nhello,Saludos,Hello again\nnew_id,Nuevo,New\n"));
	assert(&text == stable_address);
	assert(text.languages() == std::vector<std::string>({"es", "en"}));
	assert(text.text("hello", "es") == "Saludos");
	assert(text.text("new_id", "en") == "New");
	assert(!text.has("fallback"));

	assets::ImageLocalization images(bytes("id,en,es\nlogo,assets://ui/Logo.png,assets://ui/Logo ES.png\n"));
	assert(images.image("logo", "es") == "assets://ui/Logo ES.png");
	assert(images.image("missing", "es").empty());

	ui::UISystemInterface system;
	Rml::String resolved;
	system.JoinPath(resolved, "assets://UI/Sub/Panel.rml", "../styles/UI Style.rcss");
	assert(resolved == "assets://UI/styles/UI Style.rcss");
	system.JoinPath(resolved, "assets://UI/Sub/Panel.rml", "core://ui/./styles/../Base.rcss");
	assert(resolved == "core://ui/Base.rcss");
	system.JoinPath(resolved, "assets://UI/Sub/Panel.rml", ".\\local style.rcss");
	assert(resolved == "assets://UI/Sub/local style.rcss");
}
