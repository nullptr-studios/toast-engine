/**
 * @file checks.cpp
 * @author Xein
 * @date 25 Oct 2026
 * @brief Checks for the upload path
 */

#include "storage.hpp"
#include "remote_config.hpp"
#include "upload.hpp"

#include <asio.hpp>
#include <zlib.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Check {
	const char* name;
	std::function<void()> run;
};

std::vector<Check>& registry() {
	static std::vector<Check> checks;
	return checks;
}

struct Register {
	Register(const char* name, std::function<void()> run) { registry().push_back({name, std::move(run)}); }
};

void check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void setEnv(const char* name, const std::string& value) {
#ifdef _WIN32
	_putenv_s(name, value.c_str());
#else
	setenv(name, value.c_str(), 1);
#endif
}

/// fresh directory for the session csv
struct TempConfigDir {
	fs::path root;
	TempConfigDir() {
		root = fs::temp_directory_path() / ("log_server_checks_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(root);
#ifdef _WIN32
		setEnv("APPDATA", root.string());
#else
		setEnv("XDG_CONFIG_HOME", root.string());
#endif
	}
	~TempConfigDir() {
		std::error_code ec;
		fs::remove_all(root, ec);
	}
};

#define CHECK(name) \
	void name(); \
	const Register register_##name(#name, name); \
	void name()

CHECK(finish_session_returns_summary_once) {
	TempConfigDir dir;
	log_server::LogStorage storage;
	const auto path = storage.beginSession();
	check(path.has_value(), "beginSession opens a file");
	storage.addLog(log_server::serverLog(log_server::LogData::INFO, "x"));

	const auto summary = storage.finishSession();
	check(summary.has_value(), "first finishSession returns a summary");
	check(summary->path == *path, "summary path is the session file");
	check(fs::exists(summary->path), "summary file exists");
	check(summary->size > 0, "summary size is positive");
	check(summary->size == fs::file_size(summary->path), "summary size is the file size");
	check(summary->duration >= std::chrono::seconds(0), "duration is not negative");
	const auto age = std::chrono::system_clock::now() - summary->started;
	check(age >= std::chrono::seconds(-1) && age < std::chrono::minutes(1), "started is about now");

	check(!storage.finishSession().has_value(), "second finishSession returns nothing");
}

CHECK(finish_session_without_begin_returns_nothing) {
	log_server::LogStorage storage;
	check(!storage.finishSession().has_value(), "no session, no summary");
}

CHECK(finish_session_concurrent_yields_one_summary) {
	TempConfigDir dir;
	log_server::LogStorage storage;
	check(storage.beginSession().has_value(), "beginSession opens a file");
	storage.addLog(log_server::serverLog(log_server::LogData::INFO, "x"));

	std::atomic<int> winners = 0;
	std::vector<std::thread> threads;
	for (int i = 0; i < 8; ++i) {
		threads.emplace_back([&] {
			if (storage.finishSession().has_value()) {
				++winners;
			}
		});
	}
	for (auto& thread : threads) {
		thread.join();
	}
	check(winners == 1, "exactly one thread gets the summary");
}

/// one thread http server for the checks
class FakeHttp {
public:
	struct Request {
		std::string method, target, headers, body;
	};
	struct Reply {
		int status = 200;
		std::string body;
	};
	using Handler = std::function<Reply(const Request&)>;

	explicit FakeHttp(Handler handler)
	    : handler(std::move(handler)), acceptor(context, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)) {
		worker = std::thread([this] { serve(); });
	}
	~FakeHttp() {
		stopping = true;
		asio::error_code ec;
		acceptor.close(ec);
		worker.join();
	}

	[[nodiscard]] std::uint16_t port() const { return acceptor.local_endpoint().port(); }
	[[nodiscard]] std::string url() const { return "http://127.0.0.1:" + std::to_string(port()); }
	[[nodiscard]] std::vector<Request> requests() {
		std::scoped_lock lock(mutex);
		return seen;
	}

private:
	void serve() {
		while (!stopping) {
			asio::ip::tcp::socket socket(context);
			asio::error_code ec;
			acceptor.accept(socket, ec);
			if (ec) {
				continue;
			}
			handle(socket);
		}
	}

	void handle(asio::ip::tcp::socket& socket) {
		asio::error_code ec;
		asio::streambuf buffer;
		asio::read_until(socket, buffer, "\r\n\r\n", ec);
		if (ec) {
			return;
		}
		std::string all(asio::buffers_begin(buffer.data()), asio::buffers_end(buffer.data()));
		const std::size_t head_end = all.find("\r\n\r\n") + 4;
		Request request;
		request.headers = all.substr(0, head_end);
		const std::size_t line_end = request.headers.find("\r\n");
		const std::string line = request.headers.substr(0, line_end);
		request.method = line.substr(0, line.find(' '));
		request.target = line.substr(line.find(' ') + 1, line.rfind(' ') - line.find(' ') - 1);
		request.body = all.substr(head_end);

		std::string lower = request.headers;
		for (char& c : lower) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (lower.find("expect: 100-continue") != std::string::npos) {
			asio::write(socket, asio::buffer(std::string("HTTP/1.1 100 Continue\r\n\r\n")), ec);
		}
		const bool chunked = lower.find("transfer-encoding: chunked") != std::string::npos;
		std::size_t length = 0;
		if (const auto at = lower.find("content-length:"); at != std::string::npos) {
			length = static_cast<std::size_t>(std::stoull(lower.substr(at + 15)));
		}
		std::array<char, 65536> chunk {};
		const auto complete = [&] { return chunked ? request.body.ends_with("0\r\n\r\n") : request.body.size() >= length; };
		while (!complete()) {
			const std::size_t got = socket.read_some(asio::buffer(chunk), ec);
			if (ec) {
				break;
			}
			request.body.append(chunk.data(), got);
		}
		const Reply reply = handler(request);
		{
			std::scoped_lock lock(mutex);
			seen.push_back(request);
		}
		const std::string out = "HTTP/1.1 " + std::to_string(reply.status) + " X\r\nContent-Type: application/json\r\nContent-Length: " +
		                        std::to_string(reply.body.size()) + "\r\nConnection: close\r\n\r\n" + reply.body;
		asio::write(socket, asio::buffer(out), ec);
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	}

	Handler handler;
	asio::io_context context;
	asio::ip::tcp::acceptor acceptor;
	std::thread worker;
	std::atomic<bool> stopping = false;
	std::mutex mutex;
	std::vector<Request> seen;
};

std::string readAll(const fs::path& path) {
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string gunzip(const std::string& data) {
	z_stream stream {};
	check(inflateInit2(&stream, 15 + 16) == Z_OK, "inflateInit2");
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
	stream.avail_in = static_cast<uInt>(data.size());
	std::string out;
	std::array<char, 65536> buffer {};
	int status = Z_OK;
	while (status == Z_OK) {
		stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
		stream.avail_out = static_cast<uInt>(buffer.size());
		status = inflate(&stream, Z_NO_FLUSH);
		out.append(buffer.data(), buffer.size() - stream.avail_out);
	}
	inflateEnd(&stream);
	check(status == Z_STREAM_END, "gzip stream ends cleanly");
	return out;
}

fs::path tempFile(const char* name, const std::string& content) {
	const fs::path path = fs::temp_directory_path() / (std::string("log_server_checks_") + name);
	std::ofstream(path, std::ios::binary) << content;
	return path;
}

log_server::UploadJob sampleJob(const fs::path& csv) {
	log_server::UploadJob job;
	job.csv = csv;
	job.host = "devbox";
	job.user = "zo\xC3\xAB";
	job.started = "2026-10-25 12:00:00.000Z";
	job.duration_s = 42;
	job.size = fs::file_size(csv);
	return job;
}

CHECK(gzip_round_trips_big_and_empty_files) {
	std::string big;
	for (int i = 0; i < 200000; ++i) {
		big += "1792684753419544000,INFO,engine.cpp," + std::to_string(i) + ",Engine,line\n";
	}
	const fs::path in = tempFile("big.csv", big);
	const fs::path out = tempFile("big.csv.gz", "");
	check(log_server::gzipFile(in, out), "gzipFile succeeds");
	check(gunzip(readAll(out)) == big, "round trip is identical");

	const fs::path empty = tempFile("empty.csv", "");
	check(log_server::gzipFile(empty, out), "empty input gzips");
	check(gunzip(readAll(out)).empty(), "empty round trip");
	std::error_code ec;
	fs::remove(in, ec);
	fs::remove(out, ec);
	fs::remove(empty, ec);
}

CHECK(format_utc_has_millisecond_precision) {
	using namespace std::chrono;
	check(log_server::formatUtc(system_clock::time_point(duration_cast<system_clock::duration>(milliseconds(1500)))) == "1970-01-01 00:00:01.500Z", "epoch plus 1.5 s");
}

CHECK(upload_posts_token_fields_and_gzip) {
	const fs::path csv = tempFile("up.csv", "a,b,c\n1,2,3\n");
	FakeHttp server([](const FakeHttp::Request& request) -> FakeHttp::Reply {
		if (request.method == "GET" && request.target == "/kenzo/token") {
			return {200, R"({"token":"T"})"};
		}
		return {200, "{}"};
	});
	setEnv("KENZO_REMOTE_URL", server.url());
	log_server::runUpload(sampleJob(csv));

	const auto seen = server.requests();
	check(seen.size() == 2, "token request then upload");
	check(seen[0].target == "/kenzo/token", "token route");
	const auto& post = seen[1];
	check(post.method == "POST" && post.target == "/api/collections/logs/records", "records route");
	check(post.headers.find("Authorization: Bearer T") != std::string::npos, "bearer header");
	for (const char* field : {"name=\"host\"", "name=\"user\"", "name=\"started\"", "name=\"duration_s\"", "name=\"size\"", "name=\"file\"; filename=\""}) {
		check(post.body.find(field) != std::string::npos, field);
	}
	check(post.body.find(".csv.gz\"") != std::string::npos, "file is named .csv.gz");
	check(post.body.find("zo\xC3\xAB") != std::string::npos, "non-ASCII user survives");
	check(post.body.find("\x1f\x8b") != std::string::npos, "gzip magic in body");
	check(!fs::exists(fs::path(csv).concat(".gz.tmp")), "temp file removed");
	std::error_code ec;
	fs::remove(csv, ec);
}

CHECK(upload_to_a_closed_port_returns_quickly_and_cleans_up) {
	std::uint16_t port = 0;
	{
		asio::io_context context;
		asio::ip::tcp::acceptor probe(context, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
		port = probe.local_endpoint().port();
	}
	setEnv("KENZO_REMOTE_URL", "http://127.0.0.1:" + std::to_string(port));
	const fs::path csv = tempFile("closed.csv", "a,b\n");
	const auto begin = std::chrono::steady_clock::now();
	log_server::runUpload(sampleJob(csv));
	check(std::chrono::steady_clock::now() - begin < std::chrono::seconds(12), "returns within 12 s");
	check(!fs::exists(fs::path(csv).concat(".gz.tmp")), "temp file removed");
	std::error_code ec;
	fs::remove(csv, ec);
}

CHECK(upload_survives_server_errors) {
	FakeHttp server([](const FakeHttp::Request&) -> FakeHttp::Reply { return {500, "boom"}; });
	setEnv("KENZO_REMOTE_URL", server.url());
	const fs::path csv = tempFile("err.csv", "a,b\n");
	log_server::runUpload(sampleJob(csv));
	check(!fs::exists(fs::path(csv).concat(".gz.tmp")), "temp file removed");
	check(server.requests().size() == 1, "no upload without a token");
	std::error_code ec;
	fs::remove(csv, ec);
}

CHECK(an_upload_that_takes_too_long_is_dropped) {
	// takes the request and never answers
	FakeHttp server([](const FakeHttp::Request& request) -> FakeHttp::Reply {
		if (request.target == "/kenzo/token") {
			return {200, R"({"token":"T"})"};
		}
		std::this_thread::sleep_for(std::chrono::seconds(8));
		return {200, "{}"};
	});
	setEnv("KENZO_REMOTE_URL", server.url());
	const fs::path csv = tempFile("slow.csv", "a,b\n");
	const long saved = log_server::g_upload_timeout_seconds;
	log_server::g_upload_timeout_seconds = 2;
	const auto begin = std::chrono::steady_clock::now();
	log_server::runUpload(sampleJob(csv));
	const auto took = std::chrono::steady_clock::now() - begin;
	log_server::g_upload_timeout_seconds = saved;
	check(took < std::chrono::seconds(6), "gives up at the limit instead of waiting for the server");
	check(!fs::exists(fs::path(csv).concat(".gz.tmp")), "temp file removed");
	check(saved == 300, "the default limit is five minutes");
	std::error_code ec;
	fs::remove(csv, ec);
}

CHECK(host_and_user_names_are_never_empty) {
	check(!log_server::hostName().empty(), "host name");
	check(!log_server::userName().empty(), "user name");
}

CHECK(upload_args_round_trip_with_spaces_and_unicode) {
	const fs::path csv = tempFile("args.csv", "a\n");
	auto job = sampleJob(csv);
	job.user = "Ana Mar\xC3\xAD" "a";
	job.host = "caf\xC3\xA9" " box";
	const auto parsed = log_server::parseUploadArgs(log_server::uploadArgs(job));
	check(parsed.has_value(), "parses");
	check(parsed->csv == job.csv && parsed->host == job.host && parsed->user == job.user, "strings survive");
	check(parsed->started == job.started && parsed->duration_s == job.duration_s && parsed->size == job.size, "numbers survive");
	std::error_code ec;
	fs::remove(csv, ec);
}

CHECK(upload_args_reject_incomplete_input) {
	check(!log_server::parseUploadArgs({}).has_value(), "empty");
	check(!log_server::parseUploadArgs({"--other"}).has_value(), "not an upload");
	check(!log_server::parseUploadArgs({"--upload"}).has_value(), "missing csv");
	check(!log_server::parseUploadArgs({"--upload", "x.csv", "--host"}).has_value(), "missing value");
	check(!log_server::parseUploadArgs({"--upload", "x.csv", "--duration", "abc"}).has_value(), "bad number");
}

CHECK(spawn_uploader_respects_opt_out_and_empty_files) {
	const fs::path csv = tempFile("spawn.csv", "a,b\n");
	log_server::SessionSummary summary;
	summary.path = csv;
	summary.started = std::chrono::system_clock::now();
	summary.duration = std::chrono::seconds(5);
	summary.size = fs::file_size(csv);

	std::vector<std::vector<std::string>> spawned;
	log_server::g_spawn_override = [&](const std::vector<std::string>& args) {
		spawned.push_back(args);
		return true;
	};

	setEnv("KENZO_REMOTE_URL", "http://127.0.0.1:1");
	setEnv("KENZO_NO_UPLOAD", "1");
	log_server::spawnUploader(summary);
	check(spawned.empty(), "opt-out spawns nothing");

	setEnv("KENZO_NO_UPLOAD", "");
	auto empty = summary;
	empty.size = 0;
	log_server::spawnUploader(empty);
	check(spawned.empty(), "zero size spawns nothing");

	auto missing = summary;
	missing.path = csv.parent_path() / "does_not_exist.csv";
	log_server::spawnUploader(missing);
	check(spawned.empty(), "missing file spawns nothing");

	log_server::spawnUploader(summary);
	check(spawned.size() == 1, "a real session spawns once");
	const auto job = log_server::parseUploadArgs(spawned[0]);
	check(job.has_value() && job->csv == csv && job->duration_s == 5 && job->size == summary.size, "arguments describe the session");

	log_server::g_spawn_override = nullptr;
	std::error_code ec;
	fs::remove(csv, ec);
}

CHECK(the_built_in_server_is_used_unless_overridden) {
	setEnv("KENZO_REMOTE_URL", "");
	check(log_server::remoteBaseUrl() == "http://kenzo.nullptr.es:8080", "the built-in server");
	setEnv("KENZO_REMOTE_URL", "http://127.0.0.1:9/");
	check(log_server::remoteBaseUrl() == "http://127.0.0.1:9", "an environment override wins, trailing slash trimmed");
	setEnv("KENZO_REMOTE_URL", "");
}

CHECK(finish_and_upload_spawns_once_even_when_called_twice) {
	TempConfigDir dir;
	setEnv("KENZO_NO_UPLOAD", "");
	setEnv("KENZO_REMOTE_URL", "http://127.0.0.1:1");
	int spawns = 0;
	log_server::g_spawn_override = [&](const std::vector<std::string>&) {
		++spawns;
		return true;
	};
	log_server::LogStorage storage;
	check(storage.beginSession().has_value(), "beginSession");
	storage.addLog(log_server::serverLog(log_server::LogData::INFO, "x"));
	log_server::finishAndUpload(storage);
	log_server::finishAndUpload(storage);
	log_server::g_spawn_override = nullptr;
	check(spawns == 1, "one uploader for one session");
}

}  // namespace

int main() {
	int failed = 0;
	for (const auto& entry : registry()) {
		try {
			entry.run();
			std::printf("ok   %s\n", entry.name);
		} catch (const std::exception& e) {
			std::printf("FAIL %s: %s\n", entry.name, e.what());
			++failed;
		}
	}
	std::printf("%zu checks, %d failed\n", registry().size(), failed);
	return failed == 0 ? 0 : 1;
}
