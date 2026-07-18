/**
 * @file document_preprocess.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Prepares .rml text before it reaches RmlUi
 */

#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace ui {

struct PreprocessContext {
	std::vector<std::string> style_uris;
	bool inject_data_model = false;
};

struct DocumentScan {
	std::string transformed_rml;
	std::vector<std::string> binds;
	std::vector<std::string> bool_binds;
	std::vector<std::string> events;
};

/**
 * @brief Scans and transforms a document before RmlUi parses it
 *
 * Injects `<link type="text/rcss">` tags for the given VFS style URIs
 *
 * Ccollects data binding names from `data-*` attributes and `{{ }}` expressions
 *
 * Ccollects event handler names from 1data-event-*` and `on*` attributes
 *
 * Tags `<body>` with the shared data-model
 */
auto preprocessDocument(std::string_view rml, const PreprocessContext& ctx) -> DocumentScan;

}
