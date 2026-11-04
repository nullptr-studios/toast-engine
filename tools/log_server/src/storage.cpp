/**
 * @file storage.cpp
 * @author Xein
 * @date 25 Oct 2026
 */

#include "storage.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace log_server {

namespace {

namespace fs = std::filesystem;

constexpr std::array<std::string_view, 8> csv_header = {
	"Timestamp", "Severity", "Filepath", "Line", "Sink", "Message", "Details", "Backtrace",
};

/// suffixes to try when a name is taken
constexpr std::uint32_t max_name_attempts = 1000;

std::optional<fs::path> envPath(const char* name) {
#ifdef _WIN32
	// getenv mangles non ascii names
	const std::wstring wide_name(name, name + std::char_traits<char>::length(name));
	const DWORD size = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
	if (size <= 1) {
		return std::nullopt;
	}
	std::wstring value(size, L'\0');
	const DWORD written = GetEnvironmentVariableW(wide_name.c_str(), value.data(), size);
	value.resize(written);
	if (value.empty()) {
		return std::nullopt;
	}
	return fs::path(value);
#else
	const char* value = std::getenv(name);
	if (value == nullptr || *value == '\0') {
		return std::nullopt;
	}
	return fs::path(value);
#endif
}

std::string_view severityText(int severity) {
	switch (severity) {
		case 0: return "TRACE";
		case 1: return "INFO";
		case 2: return "WARNING";
		case 3: return "ERROR";
		case 4: return "CRITICAL";
		default: return "UNKNOWN";
	}
}

/// quoted only when needed
void appendField(std::string& out, std::string_view field) {
	if (field.find_first_of(",\"\r\n") == std::string_view::npos) {
		out.append(field);
		return;
	}
	out.push_back('"');
	for (const char c : field) {
		if (c == '"') {
			out.push_back('"');
		}
		out.push_back(c);
	}
	out.push_back('"');
}

void appendRecord(std::string& out, std::span<const std::string_view> fields) {
	bool first = true;
	for (const auto field : fields) {
		if (!first) {
			out.push_back(',');
		}
		first = false;
		appendField(out, field);
	}
	out.push_back('\n');
}

void appendLog(std::string& out, const LogData& log) {
	const std::string timestamp = std::to_string(log.timestamp());
	const std::string line = std::to_string(log.line_number());
	const std::array<std::string_view, 8> fields = {
		timestamp, severityText(log.severity()), log.filepath(), line, log.sink(), log.message(), log.details(), log.backtrace(),
	};
	appendRecord(out, fields);
}

#ifdef _WIN32
using NativeFile = HANDLE;
inline const NativeFile no_file = INVALID_HANDLE_VALUE;

/// creates a file that must not exist
NativeFile createNewFile(const fs::path& path, std::error_code& ec) {
	const HANDLE handle = CreateFileW(
	    path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr
	);
	if (handle == INVALID_HANDLE_VALUE) {
		ec.assign(static_cast<int>(GetLastError()), std::system_category());
	}
	return handle;
}

bool writeSome(NativeFile file, const char* data, std::size_t size, std::size_t& written, std::error_code& ec) {
	DWORD count = 0;
	if (!WriteFile(file, data, static_cast<DWORD>(std::min<std::size_t>(size, 1u << 30)), &count, nullptr)) {
		ec.assign(static_cast<int>(GetLastError()), std::system_category());
		return false;
	}
	written = count;
	return true;
}

bool syncFile(NativeFile file, std::error_code& ec) {
	if (!FlushFileBuffers(file)) {
		ec.assign(static_cast<int>(GetLastError()), std::system_category());
		return false;
	}
	return true;
}

void closeFile(NativeFile file) {
	CloseHandle(file);
}
#else
using NativeFile = int;
constexpr NativeFile no_file = -1;

NativeFile createNewFile(const fs::path& path, std::error_code& ec) {
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
	if (fd < 0) {
		ec.assign(errno, std::generic_category());
	}
	return fd;
}

bool writeSome(NativeFile file, const char* data, std::size_t size, std::size_t& written, std::error_code& ec) {
	for (;;) {
		const ssize_t count = ::write(file, data, size);
		if (count >= 0) {
			written = static_cast<std::size_t>(count);
			return true;
		}
		if (errno != EINTR) {
			ec.assign(errno, std::generic_category());
			return false;
		}
	}
}

bool syncFile(NativeFile file, std::error_code& ec) {
	if (::fsync(file) != 0) {
		ec.assign(errno, std::generic_category());
		return false;
	}
	return true;
}

void closeFile(NativeFile file) {
	::close(file);
}
#endif

/// adds a numeric suffix when the name is taken
std::expected<std::pair<NativeFile, fs::path>, std::error_code> createUniqueFile(const fs::path& dir, const std::string& stamp) {
	for (std::uint32_t attempt = 0; attempt < max_name_attempts; ++attempt) {
		const std::string name = attempt == 0 ? stamp + ".csv" : stamp + "-" + std::to_string(attempt) + ".csv";
		fs::path path = dir / name;
		std::error_code ec;
		const NativeFile file = createNewFile(path, ec);
		if (!ec) {
			return std::pair {file, std::move(path)};
		}
		if (ec != std::errc::file_exists) {
			return std::unexpected(ec);
		}
	}
	return std::unexpected(std::make_error_code(std::errc::file_exists));
}

std::string sessionStamp() {
	const std::time_t now = std::time(nullptr);
	std::tm local {};
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	std::array<char, 32> text {};
	const std::size_t size = std::strftime(text.data(), text.size(), "%Y-%m-%d_%H-%M-%S", &local);
	return std::string(text.data(), size);
}

void report(const char* what, const std::error_code& ec) {
	std::fprintf(stderr, "%s: %s\n", what, ec.message().c_str());
}

}  // namespace

std::uint64_t nowNanos() {
	const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
	const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch).count();
	return nanos < 0 ? 0 : static_cast<std::uint64_t>(nanos);
}

std::string toUtf8(const fs::path& path) {
	const auto text = path.u8string();
	return std::string(text.begin(), text.end());
}

std::vector<fs::path> candidateLogDirs() {
	std::vector<fs::path> dirs;

#ifdef _WIN32
	const std::optional<fs::path> config_dir = envPath("APPDATA");
#else
	std::optional<fs::path> config_dir = envPath("XDG_CONFIG_HOME");
	if (!config_dir) {
		if (const auto home = envPath("HOME")) {
			config_dir = *home / ".config";
		}
	}
#endif

	if (config_dir) {
		dirs.push_back(*config_dir / "ToastEngine" / "logs");
	}
	dirs.emplace_back("./logs");
	std::error_code ec;
	auto temp = fs::temp_directory_path(ec);
	if (ec) {
		temp = ".";
	}
	dirs.push_back(temp / "ToastEngine" / "logs");
	return dirs;
}

LogData serverLog(LogData::Severity severity, std::string message) {
	LogData log;
	log.set_timestamp(nowNanos());
	log.set_severity(severity);
	log.set_sink("Log Server");
	log.set_message(std::move(message));
	return log;
}

/// one session csv
class SessionWriter {
public:
	SessionWriter(const SessionWriter&) = delete;
	SessionWriter& operator=(const SessionWriter&) = delete;

	~SessionWriter() {
		closeFile(file);
	}

	/// writes the header
	static std::expected<std::unique_ptr<SessionWriter>, std::error_code> start(NativeFile file) {
		std::unique_ptr<SessionWriter> writer(new SessionWriter(file));
		appendRecord(writer->buf, csv_header);
		if (std::error_code ec = writer->commit()) {
			return std::unexpected(ec);
		}
		return writer;
	}

	/// formats in memory then writes once
	std::error_code append(const LogBatch& batch) {
		for (const auto& log : batch.logs()) {
			appendLog(buf, log);
		}
		return commit();
	}

	std::error_code finish() {
		std::error_code ec;
		syncFile(file, ec);
		return ec;
	}

private:
	explicit SessionWriter(NativeFile file) : file(file) {}

	/// a failed write makes the next one start with a newline
	std::error_code commit() {
		if (buf.empty()) {
			return {};
		}
		if (needs_newline) {
			buf.insert(buf.begin(), '\n');
		}
		std::error_code ec;
		const char* data = buf.data();
		std::size_t left = buf.size();
		while (left > 0 && !ec) {
			std::size_t written = 0;
			if (writeSome(file, data, left, written, ec)) {
				data += written;
				left -= written;
			}
		}
		buf.clear();
		needs_newline = static_cast<bool>(ec);
		return ec;
	}

	NativeFile file = no_file;
	/// rows are formatted here first
	std::string buf;
	/// the last write failed
	bool needs_newline = false;
};

LogStorage::LogStorage() = default;
LogStorage::~LogStorage() = default;

std::expected<fs::path, std::string> LogStorage::beginSession() {
	const std::string stamp = sessionStamp();
	std::optional<std::error_code> last_error;

	for (const auto& candidate : candidateLogDirs()) {
		std::error_code ec;
		fs::path dir = fs::absolute(candidate, ec);
		if (ec) {
			dir = candidate;
			ec.clear();
		}
		dir = dir.lexically_normal();
		if (dir.has_filename() == false && dir.has_parent_path()) {
			dir = dir.parent_path();
		}

		fs::create_directories(dir, ec);
		if (ec) {
			last_error = ec;
			continue;
		}
		auto created = createUniqueFile(dir, stamp);
		if (!created) {
			last_error = created.error();
			continue;
		}
		auto [file, path] = std::move(*created);
		auto writer = SessionWriter::start(file);
		if (!writer) {
			last_error = writer.error();  // start closed the file
			continue;
		}
		{
			std::scoped_lock lock(session_mutex);
			session = std::move(*writer);
			session_path = path;
			session_started = std::chrono::system_clock::now();
			session_began = std::chrono::steady_clock::now();
		}
		addLog(serverLog(LogData::INFO, "Saving logs to " + toUtf8(path)));
		return path;
	}

	return std::unexpected(last_error ? last_error->message() : std::string("no log directory candidates"));
}

void LogStorage::addLogs(LogBatch&& batch) {
	// on disk before it is broadcast
	{
		std::scoped_lock lock(session_mutex);
		if (session) {
			if (const std::error_code ec = session->append(batch)) {
				report("Failed to write session log", ec);
			}
		}
	}

	for (auto& log : *batch.mutable_logs()) {
		// broadcast first then store
		for (const auto& [id, subscriber] : subscribers) {
			subscriber(log);
		}
		logs.push_back(std::move(log));
	}
}

void LogStorage::addLog(LogData&& log) {
	LogBatch batch;
	*batch.add_logs() = std::move(log);
	addLogs(std::move(batch));
}

LogBatch LogStorage::snapshot() const {
	LogBatch batch;
	batch.mutable_logs()->Reserve(static_cast<int>(logs.size()));
	for (const auto& log : logs) {
		*batch.add_logs() = log;
	}
	return batch;
}

std::uint64_t LogStorage::subscribe(Subscriber subscriber) {
	const std::uint64_t id = next_subscriber++;
	subscribers.emplace(id, std::move(subscriber));
	return id;
}

void LogStorage::unsubscribe(std::uint64_t id) {
	subscribers.erase(id);
}

std::optional<SessionSummary> LogStorage::finishSession() {
	std::unique_ptr<SessionWriter> writer;
	SessionSummary summary;
	{
		std::scoped_lock lock(session_mutex);
		writer = std::move(session);
		summary.path = std::move(session_path);
		summary.started = session_started;
		summary.duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - session_began);
	}
	if (!writer) {
		return std::nullopt;
	}
	if (const std::error_code ec = writer->finish()) {
		report("Failed to finish session log", ec);
	}
	// the size is only final after closing
	writer.reset();
	std::error_code ec;
	summary.size = fs::file_size(summary.path, ec);
	if (ec) {
		summary.size = 0;
	}
	return summary;
}

}  // namespace log_server
