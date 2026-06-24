/**
 * @file light.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "node_3d.hpp"

#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Hidden, Icon("Light")]] TOAST_API Light : public Node3D {
public:
	Light() { }

private:
};
}
