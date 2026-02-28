/**
 * @file UiLogger.hpp
 * @author Dante Harper
 * @date 25/02/26
 *
 * @brief rewrite of darios ai generated bullshit
 */

#pragma once

#include "Ultralight/platform/Logger.h"

namespace ui {

class UiLogger : public ultralight::Logger {
	UiLogger() = default;

public:
	[[nodiscard]]
	static auto get() -> UiLogger&;

	void LogMessage(ultralight::LogLevel log_level, const ultralight::String& message) override;
};
}
