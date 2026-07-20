#include "localization.hpp"

#include "../csv.hpp"

#include <toast/log.hpp>

namespace assets {

LocalizationBase::LocalizationBase(std::vector<uint8_t> data) {
	reload(std::move(data));
}

void LocalizationBase::reload(std::vector<uint8_t> data) {
	m_languages.clear();
	m_ids.clear();
	m_rows.clear();
	const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
	const ui::CsvTable table = ui::parseCsv(text);

	if (table.empty()) {
		return;
	}

	// Header: id,<lang>,<lang>,...
	// the id column header is skipped
	const ui::CsvRow& header = table.front();
	for (size_t col = 1; col < header.size(); col++) {
		m_languages.push_back(header[col]);
	}

	for (size_t r = 1; r < table.size(); r++) {
		const ui::CsvRow& row = table[r];
		if (row.empty() || row[0].empty()) {
			continue;
		}
		const std::string& id = row[0];

		std::vector<std::string> cells(m_languages.size());
		for (size_t col = 0; col < m_languages.size(); col++) {
			const size_t src = col + 1;
			cells[col] = src < row.size() ? row[src] : std::string();
		}

		m_ids.push_back(id);
		m_rows.emplace(id, std::move(cells));
	}
}

auto LocalizationBase::languageIndex(std::string_view language) const -> int {
	for (size_t i = 0; i < m_languages.size(); i++) {
		if (m_languages[i] == language) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

auto LocalizationBase::has(std::string_view id) const -> bool {
	return m_rows.find(std::string(id)) != m_rows.end();
}

auto LocalizationBase::cell(std::string_view id, std::string_view language) const -> std::optional<std::string> {
	const auto it = m_rows.find(std::string(id));
	if (it == m_rows.end()) {
		return std::nullopt;
	}
	const std::vector<std::string>& cells = it->second;

	int index = languageIndex(language);
	if (index < 0) {
		// Unknown language
		index = cells.empty() ? -1 : 0;
	}
	if (index < 0 || std::cmp_greater_equal(index, cells.size())) {
		return std::nullopt;
	}

	const std::string& value = cells[static_cast<size_t>(index)];
	if (value.empty() && index != 0 && !cells.empty()) {
		return cells.front();
	}
	return value;
}

auto Localization::text(std::string_view id, std::string_view language) const -> std::string {
	if (auto value = cell(id, language)) {
		return *value;
	}
	TOAST_TRACE("UI", "Localization missing text id '{}'", id);
	return std::string(id);
}

auto ImageLocalization::image(std::string_view id, std::string_view language) const -> std::string {
	if (auto value = cell(id, language)) {
		return *value;
	}
	TOAST_TRACE("UI", "Localization missing image id '{}'", id);
	return {};
}

}
