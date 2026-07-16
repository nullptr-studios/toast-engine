/**
 * @file pch.h
 * @author Dante Harper
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <list>
#include <memory>
#include <numeric>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// renderer/vma.cpp defines VMA_IMPLEMENTATION and is excluded from this PCH
#include "toast/renderer/vulkan_common.hpp"

#include "toast/assets/asset_field_access.hpp"
#include "toast/reflect/reflect_node.hpp"

// Protocol buffs
#include "generated/logging.pb.h"
#include "generated/window_events.pb.h"
#include "generated/workspace_events.pb.h"
