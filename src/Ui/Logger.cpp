#include <Toast/Log.hpp>
#include <Toast/Ui/Logger.hpp>

namespace ui {
auto UiLogger::get() -> UiLogger& {
	static UiLogger instance;
	return instance;
}

void UiLogger::LogMessage(ultralight::LogLevel log_level, const ultralight::String& message) {
	std::string ul_msg = message.utf8().data();
	switch (log_level) {
		case ultralight::LogLevel::Error: TOAST_ERROR("[Ultralight] {}", ul_msg); break;
		case ultralight::LogLevel::Warning: TOAST_WARN("[Ultralight] {}", ul_msg); break;
		case ultralight::LogLevel::Info:
		default: TOAST_TRACE("[Ultralight] {}", ul_msg); break;
	}
}
}
