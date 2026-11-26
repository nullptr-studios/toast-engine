/**
 * @file layout.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief Where everything the console shows goes
 */

#pragma once
#include <functional>
#include <string_view>
#include <toast/console/console.hpp>
#include <toast/export.hpp>
#include <vector>

namespace toast::console {

struct Rect {
	float x = 0.0F;
	float y = 0.0F;
	float w = 0.0F;
	float h = 0.0F;
};

struct RowLayout {
	size_t entry = 0;
	Rect rect;
};

struct LogLayout {
	size_t line = 0;
	Rect rect;
};

struct Layout {
	Rect bar;
	Rect popup;
	std::vector<RowLayout> rows;
	std::vector<LogLayout> log;
	float padding = 0.0F;
	float cursor_x = 0.0F;
};

inline constexpr size_t k_popup_rows = 12;

TOAST_API auto
    layout(const View& view, float width, float height, float line_height, const std::function<float(std::string_view)>& measure)
        -> Layout;

}
