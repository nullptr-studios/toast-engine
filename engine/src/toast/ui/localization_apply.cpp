#include "localization_apply.hpp"

#include "ui_system.hpp"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace ui {

namespace {

void applyToElement(Rml::Element* element, const UISystem& ui) {
	if (element == nullptr) {
		return;
	}

	if (element->HasAttribute("data-loc-image")) {
		const Rml::String id = element->GetAttribute<Rml::String>("data-loc-image", Rml::String());
		if (!id.empty()) {
			std::string src = ui.localizedImage(id);
			if (!src.empty()) {
				element->SetAttribute("src", src);
			}
		}
	}

	for (int i = 0; i < element->GetNumChildren(); i++) {
		applyToElement(element->GetChild(i), ui);
	}
}

}

void applyImageLocalization(Rml::ElementDocument* document) {
	if (document == nullptr || !UISystem::exists()) {
		return;
	}
	applyToElement(document, UISystem::get());
}

}
