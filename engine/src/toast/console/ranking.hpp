/**
 * @file ranking.hpp
 * @author Xein
 * @date 25 Nov 2026
 *
 * @brief Putting the entries of the popup in order
 */

#pragma once
#include "model.hpp"

#include <algorithm>
#include <vector>

namespace toast::console {

struct Scored {
	Entry entry;
	int score = 0;
};

inline void rank(std::vector<Scored>& scored) {
	std::ranges::stable_sort(scored, [](const Scored& a, const Scored& b) {
		return a.score != b.score ? a.score > b.score : a.entry.label < b.entry.label;
	});
}

inline void append(std::vector<Entry>& out, std::vector<Scored>& scored) {
	rank(scored);
	for (auto& item : scored) {
		out.push_back(std::move(item.entry));
	}
}

}
