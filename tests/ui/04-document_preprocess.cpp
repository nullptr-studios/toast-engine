#include "test_registry.hpp"

#include <cassert>
#include <toast/ui/document_preprocess.hpp>

TOAST_TEST_NAMED("UI", "ui/04-document_preprocess", test_ui_04_document_preprocess) {
	ui::PreprocessContext context;
	context.inject_data_model = true;
	context.style_uris = {"assets://ui/global.rcss"};
	context.color_resolver = [](std::string_view name) -> std::optional<std::string> {
		if (name == "accent") {
			return "#336699ff";
		}
		return std::nullopt;
	};

	const auto scan = ui::preprocessDocument(
	    R"(<rml><head><style>body { color: ${color:accent}; }</style></head><body style="background: ${color:black}">{{title}}</body></rml>)",
	    context
	);
	assert(scan.transformed_rml.contains("<link type=\"text/css\" href=\"assets://ui/global.rcss\"/>"));
	assert(scan.transformed_rml.contains("color: #336699ff"));
	assert(scan.transformed_rml.contains("background: black"));
	assert(scan.transformed_rml.contains("data-model=\"binds\""));
	assert(scan.binds.size() == 1);
	assert(scan.binds.front() == "title");

	const std::string rcss = ui::resolveColorReferences(
	    ".button { color: ${color:accent}; border-color: ${color:white}; }",
	    context.color_resolver
	);
	assert(rcss == ".button { color: #336699ff; border-color: white; }");

	const std::string malformed = ui::resolveColorReferences("color: ${color:accent", context.color_resolver);
	assert(malformed == "color: ${color:accent");

	const auto legacy = ui::preprocessDocument(
	    R"(<rml><head><link type="text/rcss" href="../styles/UI Style.rcss"/></head><body/></rml>)", {}
	);
	assert(legacy.transformed_rml.contains("type=\"text/css\""));
	assert(legacy.transformed_rml.contains("../styles/UI Style.rcss"));

	const auto invalid = ui::preprocessDocument(
	    R"(<rml><head><link type="text/css" href="assets://ui/not-a-style.png"/></head><body/></rml>)", {}
	);
	assert(!invalid.transformed_rml.contains("not-a-style.png"));
}
