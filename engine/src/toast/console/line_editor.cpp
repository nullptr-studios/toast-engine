#include "line_editor.hpp"

#include <algorithm>

namespace toast::console {
namespace {

auto startsCharacter(char byte) -> bool {
	return (static_cast<unsigned char>(byte) & 0xC0U) != 0x80U;
}

auto isSeparator(char c) -> bool {
	return c == ' ' || c == '\t' || c == '.';
}

auto encode(uint32_t codepoint, std::string& out) -> bool {
	if (codepoint < 0x20U || codepoint == 0x7FU || codepoint > 0x10FFFFU || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
		return false;
	}
	if (codepoint < 0x80U) {
		out += static_cast<char>(codepoint);
	} else if (codepoint < 0x800U) {
		out += static_cast<char>(0xC0U | (codepoint >> 6U));
		out += static_cast<char>(0x80U | (codepoint & 0x3FU));
	} else if (codepoint < 0x10000U) {
		out += static_cast<char>(0xE0U | (codepoint >> 12U));
		out += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
		out += static_cast<char>(0x80U | (codepoint & 0x3FU));
	} else {
		out += static_cast<char>(0xF0U | (codepoint >> 18U));
		out += static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU));
		out += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
		out += static_cast<char>(0x80U | (codepoint & 0x3FU));
	}
	return true;
}

}

void LineEditor::insert(uint32_t codepoint) {
	std::string encoded;
	if (encode(codepoint, encoded)) {
		m_text.insert(m_cursor, encoded);
		m_cursor += encoded.size();
	}
}

void LineEditor::insert(std::string_view utf8) {
	for (size_t i = 0; i < utf8.size();) {
		const auto lead = static_cast<unsigned char>(utf8[i]);
		const size_t length = lead < 0x80U ? 1 : (lead >= 0xF0U ? 4 : (lead >= 0xE0U ? 3 : (lead >= 0xC0U ? 2 : 1)));
		if (lead < 0x80U) {
			insert(static_cast<uint32_t>(lead));
		} else if (lead >= 0xC0U && i + length <= utf8.size()) {
			uint32_t codepoint = lead & (0xFFU >> (length + 1));
			for (size_t k = 1; k < length; ++k) {
				codepoint = (codepoint << 6U) | (static_cast<unsigned char>(utf8[i + k]) & 0x3FU);
			}
			insert(codepoint);
		}
		i += length;
	}
}

void LineEditor::backspace() {
	if (m_cursor == 0) {
		return;
	}
	size_t begin = m_cursor - 1;
	while (begin > 0 && !startsCharacter(m_text[begin])) {
		--begin;
	}
	m_text.erase(begin, m_cursor - begin);
	m_cursor = begin;
}

void LineEditor::del() {
	if (m_cursor >= m_text.size()) {
		return;
	}
	size_t end = m_cursor + 1;
	while (end < m_text.size() && !startsCharacter(m_text[end])) {
		++end;
	}
	m_text.erase(m_cursor, end - m_cursor);
}

void LineEditor::left() {
	if (m_cursor == 0) {
		return;
	}
	--m_cursor;
	while (m_cursor > 0 && !startsCharacter(m_text[m_cursor])) {
		--m_cursor;
	}
}

void LineEditor::right() {
	if (m_cursor >= m_text.size()) {
		return;
	}
	++m_cursor;
	while (m_cursor < m_text.size() && !startsCharacter(m_text[m_cursor])) {
		++m_cursor;
	}
}

void LineEditor::home() {
	m_cursor = 0;
}

void LineEditor::end() {
	m_cursor = m_text.size();
}

void LineEditor::wordLeft() {
	while (m_cursor > 0 && isSeparator(m_text[m_cursor - 1])) {
		--m_cursor;
	}
	while (m_cursor > 0 && !isSeparator(m_text[m_cursor - 1])) {
		--m_cursor;
	}
}

void LineEditor::wordRight() {
	while (m_cursor < m_text.size() && isSeparator(m_text[m_cursor])) {
		++m_cursor;
	}
	while (m_cursor < m_text.size() && !isSeparator(m_text[m_cursor])) {
		++m_cursor;
	}
}

void LineEditor::set(std::string text) {
	m_text = std::move(text);
	m_cursor = m_text.size();
}

void LineEditor::replace(size_t begin, size_t end, std::string_view text) {
	end = std::min(end, m_text.size());
	begin = std::min(begin, end);
	m_text.replace(begin, end - begin, text);
	m_cursor = begin + text.size();
}

}
