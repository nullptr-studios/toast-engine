#include "csv.hpp"

namespace ui {

auto parseCsv(std::string_view text) -> CsvTable {
	CsvTable table;
	CsvRow row;
	std::string field;
	bool in_quotes = false;
	bool field_started = false;    // distinguishes an empty trailing line from a real empty field

	auto pushField = [&] {
		row.push_back(std::move(field));
		field.clear();
		field_started = false;
	};
	auto pushRow = [&] {
		pushField();
		table.push_back(std::move(row));
		row.clear();
	};

	for (size_t i = 0; i < text.size(); i++) {
		const char c = text[i];

		if (in_quotes) {
			if (c == '"') {
				if (i + 1 < text.size() && text[i + 1] == '"') {
					field.push_back('"');
					i++;    // escaped quote
				} else {
					in_quotes = false;
				}
			} else {
				field.push_back(c);
			}
			continue;
		}

		switch (c) {
			case '"':
				in_quotes = true;
				field_started = true;
				break;
			case ',':
				pushField();
				field_started = true;    // a field follows the separator
				break;
			case '\r': break;          // fold CRLF into LF handling
			case '\n':
				// A blank line at EOF should not become a one-empty-field row
				if (!row.empty() || field_started || !field.empty()) {
					pushRow();
				}
				break;
			default:
				field.push_back(c);
				field_started = true;
				break;
		}
	}

	// Flush a final row without a trailing newline
	if (!row.empty() || field_started || !field.empty()) {
		pushRow();
	}

	return table;
}

namespace {

auto needsQuoting(std::string_view field) -> bool {
	return field.find_first_of(",\"\r\n") != std::string_view::npos;
}

}

auto writeCsv(const CsvTable& table) -> std::string {
	std::string out;
	for (const auto& row : table) {
		for (size_t i = 0; i < row.size(); i++) {
			if (i > 0) {
				out.push_back(',');
			}
			const std::string& field = row[i];
			if (needsQuoting(field)) {
				out.push_back('"');
				for (const char c : field) {
					if (c == '"') {
						out.push_back('"');    // double up quotes
					}
					out.push_back(c);
				}
				out.push_back('"');
			} else {
				out += field;
			}
		}
		out.push_back('\n');
	}
	return out;
}

}
