#include <Toast/Renderer/HUD/ToastLogger.hpp>

#include <Toast/Log.hpp>

ToastLogger& ToastLogger::Get() {
    static ToastLogger instance;
    return instance;
}

void ToastLogger::LogMessage(ultralight::LogLevel log_level, const ultralight::String& message) {
    std::string ul_msg = message.utf8().data();
    switch (log_level) {
        case ultralight::LogLevel::Error:
            TOAST_ERROR("[Ultralight] {}", ul_msg);
            break;
        case ultralight::LogLevel::Warning:
            TOAST_WARN("[Ultralight] {}", ul_msg);
            break;
        case ultralight::LogLevel::Info:
        default:
            TOAST_TRACE("[Ultralight] {}", ul_msg);
            break;
    }
}

