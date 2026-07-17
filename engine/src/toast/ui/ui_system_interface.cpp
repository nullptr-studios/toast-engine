#include "ui_system_interface.hpp"

#include <toast/log.hpp>
#include <toast/time.hpp>
#include <SDL3/SDL.h>
#include <tracy/Tracy.hpp>

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
	// Localization hooks in once the localization assets exist
	translated = input;
	return 0;
}

void UISystemInterface::JoinPath(Rml::String& translated_path, const Rml::String& document_path, const Rml::String& path) {
	// Absolute VFS URIs stay untouched so documents can link resources across schemes
	if (path.find("://") != Rml::String::npos) {
		translated_path = path;
		return;
	}

	// Relative paths resolve against the document's own URI
	const size_t last_slash = document_path.find_last_of('/');
	if (last_slash == Rml::String::npos) {
		translated_path = path;
		return;
	}

	Rml::String base = document_path.substr(0, last_slash + 1);
	Rml::String rest = path;
	while (rest.starts_with("../")) {
		// Pop one directory per "../", never crossing the scheme root
		const size_t scheme_end = base.find("://");
		const size_t root = scheme_end != Rml::String::npos ? scheme_end + 3 : 0;
		const size_t parent_slash = base.find_last_of('/', base.size() - 2);
		if (parent_slash == Rml::String::npos || parent_slash + 1 <= root) {
			break;
		}
		base = base.substr(0, parent_slash + 1);
		rest = rest.substr(3);
	}

	translated_path = base + rest;
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

// TODO: Handle avalonia clipboard
void UISystemInterface::SetClipboardText(const Rml::String& text) {
	if (!sdlAvailable()) {
		TOAST_WARN("UI", "Clipboard is unavailable without an SDL window");
		return;
	}
	SDL_SetClipboardText(text.c_str());
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
