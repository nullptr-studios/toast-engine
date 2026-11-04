/**
 * @file storage.hpp
 * @author Xein
 * @date 25 Oct 2026
 * @brief Log store and session csv
 */

#pragma once

#include "logging.pb.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace log_server {

using LogData = proto::logging::LogData;
using LogBatch = proto::logging::LogBatch;

/// nanoseconds since the unix epoch
std::uint64_t nowNanos();

/// where the session csv can go
std::vector<std::filesystem::path> candidateLogDirs();

/// utf8 text of a path
std::string toUtf8(const std::filesystem::path& path);

/// a row written by the server itself
LogData serverLog(LogData::Severity severity, std::string message);

class SessionWriter;

/// what is known once the file is closed
struct SessionSummary {
	std::filesystem::path path;
	std::chrono::system_clock::time_point started;
	std::chrono::seconds duration {0};
	/// bytes of the csv
	std::uint64_t size = 0;
};

/// owns every log and the session csv
class LogStorage {
public:
	using Subscriber = std::function<void(const LogData&)>;

	LogStorage();
	~LogStorage();
	LogStorage(const LogStorage&) = delete;
	LogStorage& operator=(const LogStorage&) = delete;

	/// opens the session csv in the first usable directory
	std::expected<std::filesystem::path, std::string> beginSession();

	/// writes the batch then broadcasts it
	void addLogs(LogBatch&& batch);
	void addLog(LogData&& log);

	/// copy of every row
	LogBatch snapshot() const;

	/// calls subscriber for every new row
	std::uint64_t subscribe(Subscriber subscriber);
	void unsubscribe(std::uint64_t id);

	/// closes the file and returns its summary once
	std::optional<SessionSummary> finishSession();

private:
	std::deque<LogData> logs;
	std::map<std::uint64_t, Subscriber> subscribers;
	std::uint64_t next_subscriber = 0;

	std::mutex session_mutex;
	std::unique_ptr<SessionWriter> session;
	std::filesystem::path session_path;
	std::chrono::system_clock::time_point session_started;
	std::chrono::steady_clock::time_point session_began;
};

}  // namespace log_server
