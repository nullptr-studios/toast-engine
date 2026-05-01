/**
 * @file factory.hpp
 * @author Xein
 * @date 29 Apr 26
 *
 * @brief Class in charge of creating new nodes
 */

#pragma once
#include "toast/export.hpp"

#include <cstdint>
#include <string>

namespace toast {

auto TOAST_API assignUuid() -> uint64_t;

auto TOAST_API uuidToString(uint64_t uuid) -> std::string;

auto TOAST_API uuidFromString(std::string_view b64) -> uint64_t;
}
