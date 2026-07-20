#include "ui_binds.hpp"

#include "document_preprocess.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Types.h>
#include <algorithm>
#include <toast/log.hpp>
#include <toast/world/node.hpp>

namespace ui {

UIBindStore::UIBindStore() = default;
UIBindStore::~UIBindStore() = default;

void UIBindStore::reconcile(const DocumentScan& scan) {
	std::unordered_set<std::string_view> active;
	active.reserve(scan.binds.size());
	for (const auto& name : scan.binds) {
		active.emplace(name);
	}

	std::erase_if(m_values, [&](const auto& entry) { return !active.contains(entry.first); });

	for (const auto& name : scan.binds) {
		const bool is_bool = std::ranges::find(scan.bool_binds, name) != scan.bool_binds.end();
		auto [it, inserted] = m_values.try_emplace(name, is_bool ? Rml::Variant(false) : Rml::Variant(Rml::String()));
		if (!inserted && is_bool && it->second.GetType() != Rml::Variant::BOOL) {
			it->second = Rml::Variant(false);
		}
	}
}

auto UIBindStore::has(std::string_view name) const -> bool {
	return m_values.contains(std::string(name));
}

auto UIBindStore::get(std::string_view name) const -> Rml::Variant {
	const auto it = m_values.find(std::string(name));
	return it != m_values.end() ? it->second : Rml::Variant();
}

void UIBindStore::set(std::string_view name, Rml::Variant value) {
	m_values[std::string(name)] = std::move(value);
}

UIBinds::UIBinds(Rml::Context* context, toast::Node* owner, const DocumentScan& scan, UIBindStore& store)
    : m_context(context),
      m_owner(owner),
      m_store(store) {
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
			    if (!owner->participatesIn(toast::NodeOwnerParticipation::runtime_input)) {
				    return;
			    }
			    if (!owner->hasCallable(event_name)) {
				    TOAST_WARN("UI", "UI event '{}' has no matching C++ or Lua method on panel '{}'", event_name, owner->name());
				    return;
			    }
			    owner->call<void>(event_name);
		    }
		);
	}

	// Two-way value binds
	for (const auto& name : scan.binds) {
		constructor.BindFunc(
		    name,
		    [this, name](Rml::Variant& out) { out = m_store.get(name); },
		    [this, name](const Rml::Variant& in) { set(name, in); }
		);
		m_dirty_names.emplace(name);
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
	return m_store.has(name);
}

auto UIBinds::get(std::string_view name) const -> Rml::Variant {
	return m_store.get(name);
}

void UIBinds::set(std::string_view name, Rml::Variant value) {
	const std::string key(name);
	m_store.set(key, std::move(value));
	m_dirty_names.emplace(key);
}

void UIBinds::flushDirty() {
	if (m_handle) {
		for (const auto& name : m_dirty_names) {
			m_handle.DirtyVariable(name);
		}
	}
	m_dirty_names.clear();
}

void UIBinds::flushAllDirty() {
	for (const auto& [_, binds] : s_by_node) {
		if (binds != nullptr) {
			binds->flushDirty();
		}
	}
}

auto UIBinds::forNode(const toast::Node* node) -> UIBinds* {
	const auto it = s_by_node.find(node);
	return it != s_by_node.end() ? it->second : nullptr;
}

}
