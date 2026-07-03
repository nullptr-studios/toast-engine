/**
 * @file point_light.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "light.hpp"

#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Icon("PointLight")]] TOAST_API PointLight : public Light {
public:
	PointLight() = default;

	~PointLight() override = default;

private:
};
}
