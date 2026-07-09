/**
 * @file icontroller.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Hidden base node for every input controller
 */

#pragma once

#include "node.hpp"

#include <string>
#include <string_view>

namespace event {
struct LastInputType;
}

namespace input {

class [[ToastNode, Hidden]] TOAST_API IController : public toast::Node {
public:
	[[nodiscard]]
	auto lastInputType() const noexcept -> std::string_view {
		return m_last_input_type;
	}

	[[nodiscard]]
	auto lastInputName() const noexcept -> std::string_view {
		return m_last_input_name;
	}

protected:
	void init();

	std::string m_last_input_type;
	std::string m_last_input_name;
};

}
