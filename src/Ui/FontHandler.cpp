#include "AppCore/Platform.h"
#include "Toast/Log.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <Toast/Ui/FontHandler.hpp>
#include <Ultralight/platform/FontLoader.h>
#include <cstring>

using namespace ultralight;

namespace ui {

auto UiFontLoader::get() -> UiFontLoader& {
	static UiFontLoader instance;
	return instance;
}

void UiFontLoader::DestroyBuffer(void* user_data [[maybe_unused]], void* data) {
	delete[] static_cast<char*>(data);
}

[[nodiscard]]
String UiFontLoader::fallback_font() const {
	return "Monocraft";
}

[[nodiscard]]
String UiFontLoader::fallback_font_for_characters(const String& characters, int weight, bool italic) const {
	// TOAST_TRACE("[FontLoader] fallback_font_for_characters() called, weight={} italic={}", weight, italic);
	// TOAST_ERROR("TELL DANTE IF THE ACTUALLY HAPPENS OR NOT");
	TOAST_WARN("[FontLoader] Unknown characters {} (weight={} italic = {}), using fallback font", characters.utf8().data(), weight, italic);
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

	std::vector<uint8_t> font_data;
	if (resource::Open(filename, font_data)) {
		TOAST_TRACE("[Font Loader] Loading Font ... {}", filename);
		auto* buf = new char[font_data.size()];
		std::memcpy(buf, font_data.data(), font_data.size());
		auto buffer = Buffer::Create(buf, font_data.size(), nullptr, &UiFontLoader::DestroyBuffer);
		return FontFile::Create(buffer);
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
