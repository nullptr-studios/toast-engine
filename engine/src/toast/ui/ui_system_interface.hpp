/**
 * @file ui_system_interface.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief RmlUi system hooks
 */

#pragma once
#include <RmlUi/Core/SystemInterface.h>
#include <string>
#include <unordered_map>

struct SDL_Cursor;

namespace ui {

class UISystemInterface final : public Rml::SystemInterface {
public:
	UISystemInterface() = default;
	~UISystemInterface() override;

	auto GetElapsedTime() -> double override;
	auto TranslateString(Rml::String& translated, const Rml::String& input) -> int override;
	void JoinPath(Rml::String& translated_path, const Rml::String& document_path, const Rml::String& path) override;
	auto LogMessage(Rml::Log::Type type, const Rml::String& message) -> bool override;

	void SetMouseCursor(const Rml::String& cursor_name) override;
	void SetClipboardText(const Rml::String& text) override;
	void GetClipboardText(Rml::String& text) override;

private:
	std::unordered_map<std::string, SDL_Cursor*> m_cursors;
};

}
