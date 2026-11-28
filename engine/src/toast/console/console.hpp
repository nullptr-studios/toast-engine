/**
 * @file console.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief The F12 console
 */

#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <toast/console/line_editor.hpp>
#include <toast/console/model.hpp>
#include <toast/events/listener.hpp>
#include <toast/export.hpp>
#include <utility>
#include <vector>

namespace toast::console {

struct View {
	bool open = false;
	std::string text;
	size_t cursor = 0;
	Completion completion;
	size_t selected = 0;
	std::vector<std::pair<bool, std::string>> log;

	double message_age = std::numeric_limits<double>::infinity();

	std::vector<std::string> pins;
};

class TOAST_API Console {
public:
	Console();
	~Console();

	Console(const Console&) = delete;
	auto operator=(const Console&) -> Console& = delete;

	static auto get() -> Console&;

	void attach();
	void detach();

	auto onKey(int key, int action, int mods) -> bool;

	auto onChar(uint32_t codepoint) -> bool;

	[[nodiscard]]
	auto isOpen() const -> bool {
		return m_open.load(std::memory_order_relaxed);
	}

	void open();
	void close();

	void updatePins();

	[[nodiscard]]
	auto view() const -> View;

	void setAssetCheck(std::function<bool(std::string_view)> check);

	void setNodeRoot(std::function<Box<Node>()> root);

private:
	void handleKey(int key, int mods);
	void execute();
	void accept(bool backwards);
	void applyEntry(const Completion& completion, size_t index, const std::string& text);
	void moveSelection(int step);
	void navigateHistory(bool older);
	void refresh();

	mutable std::mutex m_mutex;
	std::atomic<bool> m_open {false};
	LineEditor m_editor;
	Model m_model;
	Completion m_completion;
	size_t m_selected = 0;
	std::vector<std::string> m_history;
	int m_history_position = -1;
	std::string m_draft;
	std::vector<std::pair<bool, std::string>> m_log;
	std::vector<std::string> m_pin_lines;
	std::chrono::steady_clock::time_point m_last_pin_update;
	bool m_cycling = false;
	Completion m_cycle;
	std::string m_cycle_text;
	size_t m_cycle_index = 0;
	std::chrono::steady_clock::time_point m_last_message;
	bool m_has_message = false;
	std::set<int> m_held;
	event::Listener m_listener;
	bool m_attached = false;
};

}
