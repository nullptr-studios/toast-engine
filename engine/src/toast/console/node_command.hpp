/**
 * @file node_command.hpp
 * @author Xein
 * @date 25 Nov 2026
 *
 * @brief The `node` command
 */

#pragma once
#include "model.hpp"

#include <functional>
#include <string_view>

namespace toast::console {

using NodeRoot = std::function<Box<Node>()>;

[[nodiscard]]
auto completeNode(const NodeRoot& root, std::string_view line, size_t cursor, size_t base) -> Completion;

auto executeNode(const NodeRoot& root, std::string_view arguments) -> Result;

[[nodiscard]]
auto describeNode(const NodeRoot& root, std::string_view spec) -> Result;

void runQueuedNodeWork();

}
