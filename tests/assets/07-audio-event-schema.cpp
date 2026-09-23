#include "test_registry.hpp"

#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include <toast/assets/core_types.hpp>
#include <toast/assets/schema.hpp>
#include <toast/audio/audio_event.hpp>
#include <toml++/toml.hpp>

// The FMOD importer writes these files. An unparseable audio.schema.json left every field null, so an AudioEmitter
// asked FMOD for an empty GUID and stayed silent
TOAST_TEST_NAMED("assets", "assets/07-audio-event-schema", test_assets_07) {
	std::ifstream file(TOAST_CORE_ASSETS_DIR "/schemas/audio.schema.json");
	assert(file.is_open());
	std::stringstream json;
	json << file.rdbuf();

	assets::Schema schema(json.str());
	assets::Handle<assets::Schema> schema_handle(&schema, toast::UID(toast::UID::fromString("EsFCxxgiok0")), "");

	const toml::table table = toml::parse(
	    "schema = \"EsFCxxgiok0\"\ntype = \"event\"\nname = \"TestMusic\"\npath = \"event:/TestMusic\"\n"
	    "guid = \"{852d338b-9399-4bed-93f7-c8d2978d2969}\"\n"
	);
	assets::AudioEvent event(table, schema_handle);

	assert(event.guid() == "{852d338b-9399-4bed-93f7-c8d2978d2969}");
	assert(event.path() == "event:/TestMusic");
	assert(event.name() == "TestMusic");
	assert(event.eventType() == "event");
}
