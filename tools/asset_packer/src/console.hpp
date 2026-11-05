/**
 * @file console.hpp
 * @author Xein
 * @date 4 Nov 2026
 * @brief Small console helpers shared by the packer tools
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <format>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace console {

inline void initUtf8() {
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode = 0;
	if (GetConsoleMode(out, &mode)) {
		SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	}
#endif
}

inline std::size_t width() {
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO info;
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
		return static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
	}
#else
	winsize ws {};
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
		return ws.ws_col;
	}
#endif
	return 80;
}

/// "[=====>----] 3/10 path"
class Progress {
public:
	explicit Progress(std::size_t total) : m_total(total) { std::cout << "\x1b[?25l"; }

	~Progress() { std::cout << "\x1b[?25h" << std::flush; }

	Progress(const Progress&) = delete;
	Progress& operator=(const Progress&) = delete;

	void update(std::size_t done, const std::string& message) {
		const std::size_t cols = std::max<std::size_t>(width(), 20) - 1;
		const std::string counter = std::format(" {}/{} ", done, m_total);

		const std::size_t fixed = 2 + counter.size();    // brackets + counter
		const std::size_t barWidth = std::min<std::size_t>(40, cols > fixed ? cols - fixed : 0);
		const std::size_t filled = m_total == 0 ? 0 : barWidth * done / m_total;

		std::string bar;
		for (std::size_t i = 0; i < barWidth; ++i) {
			if (i < filled) {
				bar += (i + 1 == filled && done < m_total) ? '>' : '=';
			} else {
				bar += '-';
			}
		}

		const std::size_t used = 2 + barWidth + counter.size();
		std::string msg = message;
		const std::size_t room = cols > used ? cols - used : 0;
		if (msg.size() > room) {
			msg = msg.substr(msg.size() - room);
			while (!msg.empty() && (static_cast<unsigned char>(msg.front()) & 0xC0) == 0x80) {
				msg.erase(msg.begin());
			}
		}

		std::cout << "\r\x1b[2K[\x1b[36m" << bar << "\x1b[0m]" << counter << msg << std::flush;
	}

	void finish() { std::cout << "\r\x1b[2K" << std::flush; }

private:
	std::size_t m_total;
};

}
