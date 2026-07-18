#include "ui_binds.hpp"

#include <toast/log.hpp>
#include <toast/world/node.hpp>
#include "document_preprocess.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Types.h>

namespace ui {

UIBinds::UIBinds(Rml::Context* context, toast::Node* owner, const DocumentScan& scan) : m_context(context), m_owner(owner) {
	if (context == nullptr || owner == nullptr) {
		return;
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel(k_model_name);
	if (!constructor) {
		TOAST_ERROR("UI", "Failed to create data model '{}'", k_model_name);
		return;
	}

	// bind data-event-*="Method()" to owner->call<void>("Method")
	for (const auto& event_name : scan.events) {
		constructor.BindEventCallback(
		    event_name,
		    [owner, event_name](Rml::DataModelHandle /*handle*/, Rml::Event& /*event*/, const Rml::VariantList& /*args*/) {
			    owner->call<void>(event_name);
		    }
		);
	}

	m_handle = constructor.GetModelHandle();

	TOAST_TRACE("UI", "Data model '{}' bound {} event handler(s)", k_model_name, scan.events.size());
}

UIBinds::~UIBinds() {
	if (m_context != nullptr) {
		m_context->RemoveDataModel(k_model_name);
	}
}

}
