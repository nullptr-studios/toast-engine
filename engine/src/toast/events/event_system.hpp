/**
 * @file event_system.hpp
 * @author Dante Harper
 * @date 09 May 26
 *
 * @brief Event System Singleton
 */

#pragma once

#include "toast/events/event.hpp"
#include "toast/export.hpp"

struct TOAST_API Test : event::Event<Test> { };
