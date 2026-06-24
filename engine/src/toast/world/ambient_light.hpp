/**
 * @file ambient_light.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "node.hpp"

#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Icon("Environment")]] TOAST_API AmbientLight : public Node {
public:
	AmbientLight() = default;

	~AmbientLight() = default;

private:
};
}
