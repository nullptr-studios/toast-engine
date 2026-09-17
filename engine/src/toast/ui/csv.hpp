/**
 * @file csv.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Minimal CSV reader/writer
 */

#pragma once
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <vector>

namespace ui {

using CsvRow = std::vector<std::string>;
using CsvTable = std::vector<CsvRow>;

[[nodiscard]]
TOAST_API auto parseCsv(std::string_view text) -> CsvTable;

[[nodiscard]]
TOAST_API auto writeCsv(const CsvTable& table) -> std::string;

}
