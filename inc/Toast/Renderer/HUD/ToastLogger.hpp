#pragma once

#include <Ultralight/platform/Logger.h>
#include <string>

class ToastLogger : public ultralight::Logger {
public:
    static ToastLogger& Get();

    void LogMessage(ultralight::LogLevel log_level, const ultralight::String& message) override;

private:
    ToastLogger() = default;
};

