#include "AppCore/Platform.h"
#include "Toast/Log.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <Toast/Ui/FontHandler.hpp>
#include <Ultralight/platform/FontLoader.h>

using namespace ultralight;

namespace ui {

auto UiFontLoader::get() -> UiFontLoader& {
	static UiFontLoader instance;
	return instance;
}

void UiFontLoader::DestroyBuffer(void* user_data [[maybe_unused]], void* data) {
	// delete[] static_cast<char*>(data);
}

[[nodiscard]]
String UiFontLoader::fallback_font() const {
	return "Monocraft";
}

[[nodiscard]]
String UiFontLoader::fallback_font_for_characters(const String& characters, int weight, bool italic) const {    // NOLINT
	                                                                                                              //
	TOAST_TRACE("[FontLoader] fallback_font_for_characters() called, weight={} italic={}", weight, italic);
	TOAST_ERROR("TELL DANTE IF THE ACTUALLY HAPPENS OR NOT");
	return fallback_font();
}

RefPtr<FontFile> UiFontLoader::Load(const String& family, int weight, bool italic) {
	TOAST_TRACE("[FontLoader] Loading font: '{}' weight={} italic={}", family.utf8().data(), weight, italic);
	// Map weight values to font names
	std::string weight_name;
	if (weight >= 900) {
		weight_name = "-Black";
	} else if (weight >= 700) {
		weight_name = "-Bold";
	} else if (weight >= 600) {
		weight_name = "-SemiBold";
	} else if (weight >= 500) {
		weight_name = "-Medium";
	} else if (weight < 300) {
		weight_name = "-ExtraLight";
	} else if (weight < 400) {
		weight_name = "-Light";
	} else {
		weight_name = "-Regular";
	}

	// Build filename
	std::string filename = "FONTS/";
	filename.append(family.utf8().data()).append("/").append(family.utf8().data());
	if (!weight_name.empty()) {
		filename += weight_name;
	}

	if (italic) {
		filename += "-Italic";
	}
	filename += ".ttf";

	auto file = resource::Open(filename);
	if (file.has_value()) {
		TOAST_TRACE("[Font Loader] Loading Font ... {}", filename);
		// auto buffer = Buffer::Create(file->data(), file->size(), nullptr, &UiFontLoader::DestroyBuffer);
		return FontFile::Create(filename.data());
	}
	TOAST_WARN("Font Not Found: {} falling back onto platform", filename);
	auto* font_loader = ultralight::GetPlatformFontLoader();
	auto system_font = font_loader->Load(family, weight, italic);
	if (system_font) {
		return system_font;
	}

	TOAST_ERROR("DONT DOES NOT EXIST ON SYSTEM OR ASSETS FALLBACKING ON DEFAULT");
	return Load("Monocraft", 400, false);
}
}
