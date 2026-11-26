#include "layout.hpp"

#include <algorithm>
#include <cmath>

namespace toast::console {

auto layout(const View& view, float width, float height, float line_height, const std::function<float(std::string_view)>& measure)
    -> Layout {
	Layout out;
	out.padding = line_height * 0.15F;

	const float bar_height = line_height + (2.0F * out.padding);
	out.bar = {0.0F, height - bar_height, width, bar_height};
	out.cursor_x = out.padding + measure("> ") + measure(std::string_view {view.text}.substr(0, view.cursor));

	const float row_height = line_height;
	const size_t entries = view.completion.entries.size();
	const auto fit = static_cast<size_t>(std::max(0.0F, std::floor(((height * 0.5F) - (2.0F * out.padding)) / row_height)));
	const size_t rows = std::min({k_popup_rows, entries, fit});
	float top = out.bar.y;
	if (rows > 0) {
		const float popup_height = (static_cast<float>(rows) * row_height) + (2.0F * out.padding);
		out.popup = {0.0F, out.bar.y - popup_height, std::min(width, 48.0F * line_height), popup_height};
		top = out.popup.y;

		const size_t selected = std::min(view.selected, entries - 1);
		const size_t first = selected >= rows ? selected - rows + 1 : 0;
		for (size_t i = 0; i < rows; ++i) {
			out.rows.push_back({
			  first + i, {out.popup.x, out.popup.y + out.padding + (static_cast<float>(i) * row_height), out.popup.w, row_height}
			});
		}
	}

	const auto room = static_cast<size_t>(std::max(0.0F, std::floor(height * 0.25F / line_height)));
	const size_t shown = std::min(view.log.size(), room);
	for (size_t i = 0; i < shown; ++i) {
		const size_t line = view.log.size() - shown + i;
		const float y = top - (static_cast<float>(shown - i) * line_height);
		if (y < 0.0F) {
			continue;
		}
		out.log.push_back({
		  line, {0.0F, y, width, line_height}
		});
	}
	return out;
}

}
