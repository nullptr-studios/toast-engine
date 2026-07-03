/**
 * @file ambient_light.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "light.hpp"

#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Icon("Environment")]] TOAST_API AmbientLight : public Light {
public:
	AmbientLight() = default;

	~AmbientLight() override = default;

private:
};
}
