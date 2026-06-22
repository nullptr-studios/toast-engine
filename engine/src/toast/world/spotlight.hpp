/**
 * @file spotlight.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "light.hpp"

#include <toast/export.hpp>

namespace toast {
class [[ToastNode]] TOAST_API Spotlight : public Light {
public:
	Spotlight() = default;

	~Spotlight() = default;

private:
};
}
