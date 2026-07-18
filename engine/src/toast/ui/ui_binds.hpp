/**
 * @file ui_binds.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Panel data model owner, scripting shit
 */

#pragma once
#include <RmlUi/Core/DataModelHandle.h>
#include <string>

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

	static constexpr const char* k_model_name = "binds";

private:
	Rml::Context* m_context = nullptr;
	toast::Node* m_owner = nullptr;
	Rml::DataModelHandle m_handle;
};

}
