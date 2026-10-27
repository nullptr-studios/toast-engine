/**
 * @file server.hpp
 * @author Xein
 * @date 25 Oct 2026
 * @brief Engine and TUI listeners
 */

#pragma once

#include "storage.hpp"

#include <asio.hpp>
#include <cstdint>
#include <set>
#include <string>

namespace log_server {

/// writes a line to stdout
void note(const std::string& message);

struct TuiSession;

class Server {
public:
	explicit Server(LogStorage& storage) : storage(storage) {}

	asio::awaitable<void> runTuiListener(std::uint16_t port);

	// discovery
	asio::awaitable<void> runBeacon(std::uint16_t tui_port);

	/// serves one engine at a time
	asio::awaitable<void> runEngineListener(std::uint16_t port, bool infinite);

private:
	asio::awaitable<void> handleTuiConnection(asio::ip::tcp::socket socket);
	asio::awaitable<void> flushTuiSessions();
	void closeTuiSessions();

	LogStorage& storage;
	std::set<TuiSession*> tui_sessions;
};

}  // namespace log_server
