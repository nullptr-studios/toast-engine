/**
 * @file ui_binds.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Panel data model owner, scripting shit
 */

#pragma once
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Variant.h>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Rml {
class Context;
}

namespace toast {
class Node;
}

namespace ui {

struct DocumentScan;

class UIBinds {
public:
	UIBinds(Rml::Context* context, toast::Node* owner, const DocumentScan& scan);
	~UIBinds();

	UIBinds(const UIBinds&) = delete;
	auto operator=(const UIBinds&) -> UIBinds& = delete;
	UIBinds(UIBinds&&) = delete;
	auto operator=(UIBinds&&) -> UIBinds& = delete;

	[[nodiscard]]
	auto handle() -> Rml::DataModelHandle {
		return m_handle;
	}

	/// @returns true if `name` is one of this panel's bound values
	[[nodiscard]]
	auto has(std::string_view name) const -> bool;

	/// @returns the current value of `name`
	[[nodiscard]]
	auto get(std::string_view name) const -> Rml::Variant;

	/// Writes `name` from script and notifies the data model to refresh the UI
	void set(std::string_view name, Rml::Variant value);

	/// @returns the binds owning `node`
	[[nodiscard]]
	static auto forNode(const toast::Node* node) -> UIBinds*;

	static constexpr const char* k_model_name = "binds";

private:
	Rml::Context* m_context = nullptr;
	toast::Node* m_owner = nullptr;
	Rml::DataModelHandle m_handle;
	std::unordered_map<std::string, Rml::Variant> m_store;

	static inline std::unordered_map<const toast::Node*, UIBinds*> s_by_node;
};

}
