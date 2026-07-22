#include "ui_system_interface.hpp"

#include "ui_system.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <toast/log.hpp>
#include <toast/time.hpp>
#include <tracy/Tracy.hpp>
#include <vector>

namespace ui {

namespace {

auto sdlAvailable() -> bool {
	return SDL_WasInit(SDL_INIT_VIDEO) != 0;
}

auto systemCursorFromName(const Rml::String& name) -> SDL_SystemCursor {
	if (name == "move") {
		return SDL_SYSTEM_CURSOR_MOVE;
	}
	if (name == "pointer") {
		return SDL_SYSTEM_CURSOR_POINTER;
	}
	if (name == "resize") {
		return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
	}
	if (name == "cross") {
		return SDL_SYSTEM_CURSOR_CROSSHAIR;
	}
	if (name == "text") {
		return SDL_SYSTEM_CURSOR_TEXT;
	}
	if (name == "wait" || name == "progress") {
		return SDL_SYSTEM_CURSOR_WAIT;
	}
	if (name == "unavailable") {
		return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
	}
	return SDL_SYSTEM_CURSOR_DEFAULT;
}

}

UISystemInterface::~UISystemInterface() {
	for (auto& [_, cursor] : m_cursors) {
		SDL_DestroyCursor(cursor);
	}
}

auto UISystemInterface::GetElapsedTime() -> double {
	return Time::uptime();
}

auto UISystemInterface::TranslateString(Rml::String& translated, const Rml::String& input) -> int {
	if (UISystem::exists()) {
		if (auto localized = UISystem::get().translate(input)) {
			translated = std::move(*localized);
			return 1;
		}
	}
	translated = input;
	return 0;
}

void UISystemInterface::JoinPath(Rml::String& translated_path, const Rml::String& document_path, const Rml::String& path) {
	auto normalize = [](Rml::String value) {
		std::replace(value.begin(), value.end(), '\\', '/');
		const size_t scheme_end = value.find("://");
		Rml::String prefix = scheme_end == Rml::String::npos ? Rml::String() : value.substr(0, scheme_end + 3);
		Rml::String remainder = scheme_end == Rml::String::npos ? value : value.substr(scheme_end + 3);
		std::vector<Rml::String> segments;
		size_t cursor = 0;
		while (cursor <= remainder.size()) {
			const size_t slash = remainder.find('/', cursor);
			const Rml::String segment = remainder.substr(cursor, slash == Rml::String::npos ? Rml::String::npos : slash - cursor);
			if (!segment.empty() && segment != ".") {
				if (segment == "..") {
					if (!segments.empty()) {
						segments.pop_back();
					}
				} else {
					segments.push_back(segment);
				}
			}
			if (slash == Rml::String::npos) {
				break;
			}
			cursor = slash + 1;
		}
		Rml::String result = prefix;
		for (size_t i = 0; i < segments.size(); i++) {
			if (i > 0) {
				result += '/';
			}
			result += segments[i];
		}
		return result;
	};

	if (path.contains("://")) {
		translated_path = normalize(path);
	} else {
		const size_t last_slash = document_path.find_last_of('/');
		translated_path =
		    normalize((last_slash == Rml::String::npos ? Rml::String() : document_path.substr(0, last_slash + 1)) + path);
	}
}

auto UISystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) -> bool {
	switch (type) {
		case Rml::Log::LT_ALWAYS:
		case Rml::Log::LT_ERROR:
		case Rml::Log::LT_ASSERT: TOAST_ERROR("UI", "{}", message); break;
		case Rml::Log::LT_WARNING: TOAST_WARN("UI", "{}", message); break;
		case Rml::Log::LT_INFO: TOAST_INFO("UI", "{}", message); break;
		case Rml::Log::LT_DEBUG:
		default: TOAST_TRACE("UI", "{}", message); break;
	}
	return true;
}

void UISystemInterface::SetMouseCursor(const Rml::String& cursor_name) {
	if (!sdlAvailable()) {
		return;
	}

	auto it = m_cursors.find(cursor_name);
	if (it == m_cursors.end()) {
		SDL_Cursor* cursor = SDL_CreateSystemCursor(systemCursorFromName(cursor_name));
		if (!cursor) {
			TOAST_WARN("UI", "Failed to create cursor '{}': {}", cursor_name, SDL_GetError());
			return;
		}
		it = m_cursors.emplace(cursor_name, cursor).first;
	}

	SDL_SetCursor(it->second);
}

void UISystemInterface::SetClipboardText(const Rml::String& text) {
	if (!sdlAvailable()) {
		TOAST_WARN("UI", "Clipboard is unavailable without an SDL window");
		return;
	}
	SDL_SetClipboardText(text.c_str());
}

void UISystemInterface::ActivateKeyboard(Rml::Vector2f caret_position, float line_height) {
	if (!m_window) {
		return;
	}
	SDL_Rect area {
	  static_cast<int>(std::lround(caret_position.x)),
	  static_cast<int>(std::lround(caret_position.y)),
	  1,
	  std::max(1, static_cast<int>(std::lround(line_height)))
	};
	if (!SDL_SetTextInputArea(m_window, &area, 0)) {
		TOAST_WARN("UI", "SDL_SetTextInputArea failed: {}", SDL_GetError());
	}
	if (!SDL_StartTextInput(m_window)) {
		TOAST_WARN("UI", "SDL_StartTextInput failed: {}", SDL_GetError());
	}
}

void UISystemInterface::DeactivateKeyboard() {
	if (m_window && !SDL_StopTextInput(m_window)) {
		TOAST_WARN("UI", "SDL_StopTextInput failed: {}", SDL_GetError());
	}
}

void UISystemInterface::GetClipboardText(Rml::String& text) {
	if (!sdlAvailable()) {
		TOAST_WARN("UI", "Clipboard is unavailable without an SDL window");
		return;
	}

	char* raw = SDL_GetClipboardText();
	if (raw) {
		text = raw;
		SDL_free(raw);
	}
}

}
