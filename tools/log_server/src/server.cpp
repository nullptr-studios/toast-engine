/**
 * @file server.cpp
 * @author Xein
 * @date 25 Oct 2026
 */

#include "server.hpp"
#include "upload.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>

namespace log_server {

namespace {

using asio::ip::tcp;

/// largest frame the server accepts
constexpr std::uint32_t max_frame_len = 64 * 1024 * 1024;

/// how long to wait for the TUIs before exiting
constexpr auto tui_flush_limit = std::chrono::milliseconds(500);

/// how an engine session ended
enum class SessionEnd {
	/// the engine sent the goodbye frame
	goodbye,
	/// anything else ending the connection
	lost,
};

std::string endpointText(const tcp::endpoint& endpoint) {
	const auto address = endpoint.address();
	const std::string host = address.is_v6() ? "[" + address.to_string() + "]" : address.to_string();
	return host + ":" + std::to_string(endpoint.port());
}

tcp::acceptor listen(asio::io_context& context, std::uint16_t port) {
	// no address reuse on windows
#ifdef _WIN32
	constexpr bool reuse_address = false;
#else
	constexpr bool reuse_address = true;
#endif
	return tcp::acceptor(context, tcp::endpoint(tcp::v4(), port), reuse_address);
}

/// last row of a session
LogData buildEndLog(SessionEnd end) {
	switch (end) {
		case SessionEnd::goodbye: return serverLog(LogData::INFO, "Toast Engine exited cleanly");
		case SessionEnd::lost: return serverLog(LogData::CRITICAL, "Toast Engine connection lost (probable crash or kill)");
	}
	return serverLog(LogData::CRITICAL, "Toast Engine connection lost (probable crash or kill)");
}

/// eof is how a killed process looks
SessionEnd connectionLost(const asio::error_code& error) {
	if (error != asio::error::eof) {
		note("Engine connection error: " + error.message());
	}
	return SessionEnd::lost;
}

std::string frameBatch(const LogBatch& batch) {
	const auto size = static_cast<std::uint32_t>(batch.ByteSizeLong());
	std::string frame(4 + size, '\0');
	frame[0] = static_cast<char>(size >> 24);
	frame[1] = static_cast<char>(size >> 16);
	frame[2] = static_cast<char>(size >> 8);
	frame[3] = static_cast<char>(size);
	batch.SerializeToArray(frame.data() + 4, static_cast<int>(size));
	return frame;
}

asio::awaitable<SessionEnd> handleEngineConnection(tcp::socket& socket, LogStorage& storage) {
	const auto token = asio::as_tuple(asio::use_awaitable);
	for (;;) {
		std::array<unsigned char, 4> header {};
		if (auto [ec, read] = co_await asio::async_read(socket, asio::buffer(header), token); ec) {
			co_return connectionLost(ec);
		}
		const std::uint32_t len = (std::uint32_t(header[0]) << 24) | (std::uint32_t(header[1]) << 16) | (std::uint32_t(header[2]) << 8) |
		                          std::uint32_t(header[3]);

		if (len == 0) {
			co_return SessionEnd::goodbye;
		}

		if (len > max_frame_len) {
			note(
			    "Engine sent a frame of " + std::to_string(len) + " bytes (limit " + std::to_string(max_frame_len) +
			    "); treating the stream as corrupt"
			);
			co_return SessionEnd::lost;
		}

		std::string buf(len, '\0');
		if (auto [ec, read] = co_await asio::async_read(socket, asio::buffer(buf), token); ec) {
			co_return connectionLost(ec);
		}

		// invalid utf8 drops the frame and keeps the session
		LogBatch batch;
		if (batch.ParseFromArray(buf.data(), static_cast<int>(len))) {
			storage.addLogs(std::move(batch));
		} else {
			storage.addLog(serverLog(LogData::WARNING, "Dropped an undecodable log frame (" + std::to_string(len) + " bytes)"));
		}
	}
}

}  // namespace

void note(const std::string& message) {
	std::fwrite(message.data(), 1, message.size(), stdout);
	std::fputc('\n', stdout);
	std::fflush(stdout);
}

/// a connected TUI and its queued frames
struct TuiSession {
	explicit TuiSession(tcp::socket s) : socket(std::move(s)), wake(socket.get_executor()) {
		wake.expires_at(asio::steady_timer::time_point::max());
	}

	tcp::socket socket;
	std::deque<std::string> pending;
	bool writing = false;
	asio::steady_timer wake;

	bool idle() const { return pending.empty() && !writing; }

	void push(std::string frame) {
		// unbounded so a slow TUI never loses rows
		pending.push_back(std::move(frame));
		wake.cancel();
	}
};

constexpr std::uint16_t beacon_port = 12802;

asio::awaitable<void> Server::runBeacon(std::uint16_t tui_port) {
	auto executor = co_await asio::this_coro::executor;
	asio::ip::udp::socket socket(executor);
	asio::error_code ec;
	socket.open(asio::ip::udp::v4(), ec);
	if (ec) {
		co_return;
	}
	socket.set_option(asio::socket_base::broadcast(true), ec);
	const std::string payload = "toast-log/1 " + std::to_string(tui_port) + " " + hostName();
	const asio::ip::udp::endpoint target(asio::ip::address_v4::broadcast(), beacon_port);
	asio::steady_timer timer(executor);
	for (;;) {
		socket.send_to(asio::buffer(payload), target, 0, ec);
		timer.expires_after(std::chrono::seconds(2));
		co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
	}
}

asio::awaitable<void> Server::runTuiListener(std::uint16_t port) {
	auto executor = co_await asio::this_coro::executor;
	auto& context = static_cast<asio::io_context&>(executor.context());
	tcp::acceptor listener = listen(context, port);
	note("TUI Listener running on 0.0.0.0:" + std::to_string(port));

	for (;;) {
		tcp::socket socket = co_await listener.async_accept(asio::use_awaitable);
		asio::co_spawn(executor, handleTuiConnection(std::move(socket)), asio::detached);
	}
}

asio::awaitable<void> Server::handleTuiConnection(tcp::socket socket) {
	asio::error_code ignored;
	const auto peer = socket.remote_endpoint(ignored);
	const std::string peer_text = endpointText(peer);
	note("TUI connected: " + peer_text);

	TuiSession session(std::move(socket));
	tui_sessions.insert(&session);

	// send everything logged so far then each new log as its own frame
	// subscribe and snapshot together so no row is missed or repeated
	const std::uint64_t subscription = storage.subscribe([&session](const LogData& log) {
		LogBatch update;
		*update.add_logs() = log;
		session.push(frameBatch(update));
	});
	const std::string initial = frameBatch(storage.snapshot());

	try {
		session.writing = true;
		co_await asio::async_write(session.socket, asio::buffer(initial), asio::use_awaitable);
		session.writing = false;

		for (;;) {
			if (session.pending.empty()) {
				asio::error_code woken;
				session.wake.expires_at(asio::steady_timer::time_point::max());
				co_await session.wake.async_wait(asio::redirect_error(asio::use_awaitable, woken));
				continue;
			}
			const std::string frame = std::move(session.pending.front());
			session.pending.pop_front();
			session.writing = true;
			co_await asio::async_write(session.socket, asio::buffer(frame), asio::use_awaitable);
			session.writing = false;
		}
	} catch (const std::system_error& e) {
		note(std::string("TUI connection error: ") + e.code().message());
	}

	storage.unsubscribe(subscription);
	tui_sessions.erase(&session);
	note("TUI disconnected: " + peer_text);
}

/// closes the TUI sockets gracefully so a reset drops nothing
void Server::closeTuiSessions() {
	for (TuiSession* session : tui_sessions) {
		asio::error_code ignored;
		session->socket.shutdown(tcp::socket::shutdown_send, ignored);
		session->socket.close(ignored);
	}
}

/// lets the TUIs send what is still queued
asio::awaitable<void> Server::flushTuiSessions() {
	asio::steady_timer timer(co_await asio::this_coro::executor);
	const auto deadline = std::chrono::steady_clock::now() + tui_flush_limit;
	for (;;) {
		bool busy = false;
		for (const TuiSession* session : tui_sessions) {
			busy = busy || !session->idle();
		}
		if (!busy || std::chrono::steady_clock::now() >= deadline) {
			co_return;
		}
		timer.expires_after(std::chrono::milliseconds(5));
		asio::error_code ignored;
		co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ignored));
	}
}

asio::awaitable<void> Server::runEngineListener(std::uint16_t port, bool infinite) {
	auto executor = co_await asio::this_coro::executor;
	auto& context = static_cast<asio::io_context&>(executor.context());
	tcp::acceptor listener = listen(context, port);
	note("Engine Listener running on 0.0.0.0:" + std::to_string(port));

	// a second engine waits in the backlog
	for (;;) {
		tcp::socket socket = co_await listener.async_accept(asio::use_awaitable);
		asio::error_code ignored;
		const std::string peer_text = endpointText(socket.remote_endpoint(ignored));
		note("Engine connected: " + peer_text);

		if (auto session = storage.beginSession(); !session) {
			note("Failed to open session log: " + session.error());
		}

		const SessionEnd end = co_await handleEngineConnection(socket, storage);

		storage.addLog(buildEndLog(end));
		co_await flushTuiSessions();

		note("Engine disconnected: " + peer_text);

		finishAndUpload(storage);

		if (!infinite) {
			note("Shutting down server (no --infinite).");
			closeTuiSessions();
			std::_Exit(0);
		}
	}
}

}  // namespace log_server
