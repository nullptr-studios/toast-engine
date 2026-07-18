#include "ui_binds.hpp"

#include <toast/log.hpp>
#include <toast/world/node.hpp>
#include "document_preprocess.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Types.h>
#include <algorithm>

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

	// Data-checked binds returns a bool
	// Everything else starts as a string
	for (const auto& name : scan.binds) {
		const bool is_bool = std::ranges::find(scan.bool_binds, name) != scan.bool_binds.end();
		m_store.try_emplace(name, is_bool ? Rml::Variant(false) : Rml::Variant(Rml::String()));
	}

	// Two-way value binds
	for (const auto& name : scan.binds) {
		constructor.BindFunc(
		    name,
		    [this, name](Rml::Variant& out) {
			    const auto it = m_store.find(name);
			    if (it != m_store.end()) {
				    out = it->second;
			    }
		    },
		    [this, name](const Rml::Variant& in) { m_store[name] = in; }
		);
	}

	m_handle = constructor.GetModelHandle();
	s_by_node[owner] = this;

	TOAST_TRACE("UI", "Data model '{}' bound {} value(s), {} event(s)", k_model_name, scan.binds.size(), scan.events.size());
}

UIBinds::~UIBinds() {
	if (m_owner != nullptr) {
		const auto it = s_by_node.find(m_owner);
		if (it != s_by_node.end() && it->second == this) {
			s_by_node.erase(it);
		}
	}
	if (m_context != nullptr) {
		m_context->RemoveDataModel(k_model_name);
	}
}

auto UIBinds::has(std::string_view name) const -> bool {
	return m_store.find(std::string(name)) != m_store.end();
}

auto UIBinds::get(std::string_view name) const -> Rml::Variant {
	const auto it = m_store.find(std::string(name));
	return it != m_store.end() ? it->second : Rml::Variant();
}

void UIBinds::set(std::string_view name, Rml::Variant value) {
	const std::string key(name);
	m_store[key] = std::move(value);
	if (m_handle) {
		m_handle.DirtyVariable(key);
	}
}

auto UIBinds::forNode(const toast::Node* node) -> UIBinds* {
	const auto it = s_by_node.find(node);
	return it != s_by_node.end() ? it->second : nullptr;
}

}
