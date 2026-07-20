/**
 * @file document_preprocess.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Prepares .rml text before it reaches RmlUi
 */

#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <vector>

namespace ui {

using ColorResolver = std::function<std::optional<std::string>(std::string_view)>;

struct PreprocessContext {
	std::vector<std::string> style_uris;
	ColorResolver color_resolver;
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
auto TOAST_API preprocessDocument(std::string_view rml, const PreprocessContext& ctx) -> DocumentScan;

/**
 * @brief Replaces `${color:name}` references before RmlUi parses RML or RCSS
 *
 * Scheme colors become #RRGGBBAA
 */
auto TOAST_API resolveColorReferences(std::string_view source, const ColorResolver& resolver) -> std::string;

}
