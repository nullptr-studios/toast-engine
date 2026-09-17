#include "toast/ui/csv.hpp"

#include "test_registry.hpp"

#include <cassert>

TOAST_TEST_NAMED("ui", "ui/01-csv", test_ui_01_csv) {
	// Basic rows and columns
	{
		const auto t = ui::parseCsv("id,en,es\nhello,Hello,Hola\n");
		assert(t.size() == 2);
		assert(t[0].size() == 3);
		assert(t[0][0] == "id");
		assert(t[0][2] == "es");
		assert(t[1][1] == "Hello");
		assert(t[1][2] == "Hola");
	}

	// Trailing newline must not add an empty row
	{
		const auto t = ui::parseCsv("a,b\n1,2\n");
		assert(t.size() == 2);
	}

	// Missing trailing newline still yields the last row
	{
		const auto t = ui::parseCsv("a,b\n1,2");
		assert(t.size() == 2);
		assert(t[1][1] == "2");
	}

	// Quoted fields with embedded commas and newlines
	{
		const auto t = ui::parseCsv("id,text\ngreet,\"Hello, world\"\nmulti,\"line one\nline two\"\n");
		assert(t.size() == 3);
		assert(t[1][1] == "Hello, world");
		assert(t[2][1] == "line one\nline two");
	}

	// Escaped double quotes inside a quoted field
	{
		const auto t = ui::parseCsv("id,text\nq,\"say \"\"hi\"\"\"\n");
		assert(t.size() == 2);
		assert(t[1][1] == "say \"hi\"");
	}

	// CRLF line endings
	{
		const auto t = ui::parseCsv("a,b\r\n1,2\r\n");
		assert(t.size() == 2);
		assert(t[1][0] == "1");
		assert(t[1][1] == "2");
	}

	// Empty fields are preserved
	{
		const auto t = ui::parseCsv("id,en,es\nk,,only\n");
		assert(t.size() == 2);
		assert(t[1].size() == 3);
		assert(t[1][1].empty());
		assert(t[1][2] == "only");
	}

	// Round-trip through the writer, quoting only where required
	{
		const ui::CsvTable table {
		    {"id", "text"},
		    {"greet", "Hello, world"},
		    {"q", "say \"hi\""},
		};
		const std::string csv = ui::writeCsv(table);
		const auto reparsed = ui::parseCsv(csv);
		assert(reparsed == table);
	}
}
