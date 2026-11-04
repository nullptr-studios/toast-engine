/**
 * @file pch.h
 * @author Dante Harper
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

// renderer/core/vma.cpp defines VMA_IMPLEMENTATION and is excluded from this PCH
#include "toast/assets/asset_field_access.hpp"
#include "toast/assets/assets.hpp"
#include "toast/assets/core_types.hpp"
#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"
#include "toast/export.hpp"
#include "toast/log.hpp"
#include "toast/thread_pool.hpp"
#include "toast/time.hpp"
#include "toast/uid.hpp"
#include "toast/reflect/reflect_node.hpp"
#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"

// Protocol buffs
#include "generated/logging.pb.h"
#include "generated/window_events.pb.h"
#include "generated/workspace_events.pb.h"
