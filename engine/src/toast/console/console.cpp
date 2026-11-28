#include "console.hpp"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <toast/window/window_events.hpp>

namespace toast::console {
namespace {

constexpr size_t k_log_lines = 8;
constexpr size_t k_history_lines = 200;

auto needsSpaceAfter(EntryKind kind) -> bool {
	return kind == EntryKind::variable || kind == EntryKind::field || kind == EntryKind::function;
}

}

Console::Console() = default;

Console::~Console() = default;

auto Console::get() -> Console& {
	static Console instance;
	return instance;
}

void Console::attach() {
	const std::lock_guard lock(m_mutex);
	if (m_attached) {
		return;
	}
	m_attached = true;
	m_listener.subscribe<event::WindowKey>(
	    "console_key", [this](const event::WindowKey& e) { return onKey(e.key, e.action, e.mods); }, 127
	);
	m_listener.subscribe<event::WindowChar>("console_char", [this](const event::WindowChar& e) { return onChar(e.key); }, 127);
}

void Console::detach() {
	const std::lock_guard lock(m_mutex);
	if (!m_attached) {
		return;
	}
	m_attached = false;
	m_listener.unsubscribe<event::WindowKey>("console_key");
	m_listener.unsubscribe<event::WindowChar>("console_char");
}

auto Console::onKey(int key, int action, int mods) -> bool {
	const std::lock_guard lock(m_mutex);

	if (action == event::window_input_released) {
		return m_held.erase(key) > 0;
	}

	if (key == SDLK_F12) {
		m_held.insert(key);
		if (action == event::window_input_pressed) {
			const bool open = !m_open.load();
			m_open = open;
			if (open) {
				refresh();
			}
		}
		return true;
	}

	if (!m_open.load()) {
		return false;
	}
	m_held.insert(key);
	handleKey(key, mods);
	return true;
}

auto Console::onChar(uint32_t codepoint) -> bool {
	if (!m_open.load()) {
		return false;
	}
	const std::lock_guard lock(m_mutex);
	m_cycling = false;
	m_editor.insert(codepoint);
	m_history_position = -1;
	refresh();
	return true;
}

void Console::open() {
	const std::lock_guard lock(m_mutex);
	m_open = true;
	refresh();
}

void Console::close() {
	const std::lock_guard lock(m_mutex);
	m_open = false;
}

auto Console::view() const -> View {
	const std::lock_guard lock(m_mutex);
	View view {m_open.load(), m_editor.text(), m_editor.cursor(), m_completion, m_selected, m_log};
	view.pins = m_pin_lines;
	if (m_has_message) {
		view.message_age = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_last_message).count();
	}
	return view;
}

void Console::setNodeRoot(std::function<Box<Node>()> root) {
	const std::lock_guard lock(m_mutex);
	m_model.setNodeRoot(std::move(root));
}

void Console::setAssetCheck(std::function<bool(std::string_view)> check) {
	const std::lock_guard lock(m_mutex);
	m_model.setAssetCheck(std::move(check));
}

void Console::updatePins() {
	const auto now = std::chrono::steady_clock::now();
	if (now - m_last_pin_update < std::chrono::milliseconds(100)) {
		return;
	}
	m_last_pin_update = now;
	std::vector<std::string> lines = m_model.pinnedLines();
	const std::lock_guard lock(m_mutex);
	m_pin_lines = std::move(lines);
}

void Console::handleKey(int key, int mods) {
	const bool control = (mods & SDL_KMOD_CTRL) != 0;

	if (key != SDLK_TAB && m_cycling) {
		m_cycling = false;
		refresh();
	}
	switch (key) {
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			execute();
			m_open = false;
			return;
		case SDLK_ESCAPE: m_open = false; return;
		case SDLK_TAB: accept((mods & SDL_KMOD_SHIFT) != 0); return;
		case SDLK_UP:
			if (m_editor.text().empty() || m_history_position >= 0 || m_completion.entries.empty()) {
				navigateHistory(true);
			} else {
				moveSelection(-1);
			}
			return;
		case SDLK_DOWN:
			if (m_editor.text().empty() || m_history_position >= 0 || m_completion.entries.empty()) {
				navigateHistory(false);
			} else {
				moveSelection(1);
			}
			return;
		case SDLK_LEFT: control ? m_editor.wordLeft() : m_editor.left(); break;
		case SDLK_RIGHT: control ? m_editor.wordRight() : m_editor.right(); break;
		case SDLK_HOME: m_editor.home(); break;
		case SDLK_END: m_editor.end(); break;
		case SDLK_BACKSPACE:
			m_editor.backspace();
			m_history_position = -1;
			break;
		case SDLK_DELETE:
			m_editor.del();
			m_history_position = -1;
			break;
		case SDLK_V:
			if (control) {
				if (char* clipboard = SDL_GetClipboardText(); clipboard != nullptr) {
					std::string line {clipboard};
					std::ranges::replace(line, '\n', ' ');
					std::ranges::replace(line, '\r', ' ');
					m_editor.insert(line);
					m_history_position = -1;
					SDL_free(clipboard);
				}
			}
			break;
		default: return;
	}
	refresh();
}

void Console::execute() {
	const std::string line = m_editor.text();
	m_editor.set({});
	m_history_position = -1;
	m_draft.clear();
	if (line.find_first_not_of(" \t") != std::string::npos) {
		if (m_history.empty() || m_history.back() != line) {
			m_history.push_back(line);
			if (m_history.size() > k_history_lines) {
				m_history.erase(m_history.begin());
			}
		}
		const Result result = m_model.execute(line);
		m_pin_lines = m_model.pinnedLines();
		m_log.emplace_back(result.ok, result.message.empty() ? line : result.message);
		m_last_message = std::chrono::steady_clock::now();
		m_has_message = true;
		if (m_log.size() > k_log_lines) {
			m_log.erase(m_log.begin());
		}
	}
	refresh();
}

void Console::applyEntry(const Completion& completion, size_t index, const std::string& text) {
	const Entry& entry = completion.entries[index];
	std::string insert = entry.insert;
	if (needsSpaceAfter(entry.kind) && (completion.replace_end >= text.size() || text[completion.replace_end] != ' ')) {
		insert += ' ';
	}
	m_editor.set(text);
	m_editor.replace(completion.replace_begin, completion.replace_end, insert);
}

void Console::accept(bool backwards) {
	if (!m_cycling) {
		const size_t count = m_completion.entries.size();
		if (count == 0 || m_selected >= count) {
			return;
		}
		if (count == 1) {
			applyEntry(m_completion, m_selected, m_editor.text());
			m_history_position = -1;
			refresh();
			return;
		}
		m_cycling = true;
		m_cycle = m_completion;
		m_cycle_text = m_editor.text();
		m_cycle_index = backwards ? (m_selected + count - 1) % count : m_selected;
	} else {
		const size_t count = m_cycle.entries.size();
		m_cycle_index = backwards ? (m_cycle_index + count - 1) % count : (m_cycle_index + 1) % count;
	}

	applyEntry(m_cycle, m_cycle_index, m_cycle_text);
	m_completion = m_cycle;
	m_selected = m_cycle_index;
	m_history_position = -1;
}

void Console::moveSelection(int step) {
	const auto count = static_cast<int>(m_completion.entries.size());
	if (count == 0) {
		return;
	}
	m_selected = static_cast<size_t>((static_cast<int>(m_selected) + step + count) % count);
}

void Console::navigateHistory(bool older) {
	if (older) {
		if (m_history.empty()) {
			return;
		}
		if (m_history_position < 0) {
			m_draft = m_editor.text();
			m_history_position = static_cast<int>(m_history.size()) - 1;
		} else {
			m_history_position = std::max(0, m_history_position - 1);
		}
		m_editor.set(m_history[static_cast<size_t>(m_history_position)]);
	} else {
		if (m_history_position < 0) {
			return;
		}
		if (static_cast<size_t>(m_history_position) + 1 < m_history.size()) {
			++m_history_position;
			m_editor.set(m_history[static_cast<size_t>(m_history_position)]);
		} else {
			m_history_position = -1;
			m_editor.set(m_draft);
		}
	}
	m_completion = m_model.complete(m_editor.text(), m_editor.cursor());
	m_selected = 0;
}

void Console::refresh() {
	m_completion = m_model.complete(m_editor.text(), m_editor.cursor());
	m_selected = 0;
}

}
