/**
 * @file line_editor.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief The text of the console's one line and the
 */

#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <toast/export.hpp>

namespace toast::console {

class TOAST_API LineEditor {
public:
	void insert(uint32_t codepoint);

	void insert(std::string_view utf8);

	void backspace();
	void del();
	void left();
	void right();
	void home();
	void end();

	void wordLeft();

	void wordRight();

	void set(std::string text);

	void replace(size_t begin, size_t end, std::string_view text);

	[[nodiscard]]
	auto text() const -> const std::string& {
		return m_text;
	}

	[[nodiscard]]
	auto cursor() const -> size_t {
		return m_cursor;
	}

private:
	std::string m_text;
	size_t m_cursor = 0;
};

}
