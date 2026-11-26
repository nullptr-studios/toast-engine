/**
 * @file fuzzy.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief The matching the console's popup narrows its entries with
 */

#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>

namespace toast::console {

inline auto fuzzyScore(std::string_view pattern, std::string_view text) -> std::optional<int> {
	if (pattern.empty()) {
		return 0;
	}

	const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
	const auto word_start = [&](size_t index) {
		if (index == 0) {
			return true;
		}
		const char before = text[index - 1];
		return before == '_' || before == '.' || before == '-' || before == ' ';
	};

	int score = 0;
	size_t position = 0;
	bool first = true;
	size_t last = 0;
	for (const char wanted : pattern) {
		const char target = lower(wanted);
		while (position < text.size() && lower(text[position]) != target) {
			++position;
		}
		if (position >= text.size()) {
			return std::nullopt;
		}

		score += 10;
		if (first) {
			score += position == 0 ? 40 : (word_start(position) ? 20 : 0);
			score -= static_cast<int>(std::min<size_t>(position, 10));
		} else if (position == last + 1) {
			score += 15;
		} else {
			score -= static_cast<int>(std::min<size_t>(position - last - 1, 10));
			score += word_start(position) ? 5 : 0;
		}
		first = false;
		last = position;
		++position;
	}
	return score;
}

}
