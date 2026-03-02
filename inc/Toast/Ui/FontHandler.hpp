/**
 * @file FontHandler.hpp
 * @author Dante Harper
 * @date 25/02/26
 *
 * @brief rewrite of the ai generated dario code
 */

#pragma once

#include "Ultralight/platform/FontLoader.h"

namespace ui {

class UiFontLoader : public ultralight::FontLoader {
	UiFontLoader() = default;

	static void DestroyBuffer(void* user_data, void* data);    // not sure if we actually need this or not

public:
	static auto get() -> UiFontLoader&;

	[[nodiscard]]
	ultralight::String fallback_font() const override;

	[[nodiscard]]
	ultralight::String fallback_font_for_characters(const ultralight::String& characters, int weight, bool italic) const override;

	ultralight::RefPtr<ultralight::FontFile> Load(const ultralight::String& family, int weight, bool italic) override;
};
}
