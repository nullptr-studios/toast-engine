/**
 * @file upload.cpp
 * @author Xein
 * @date 25 Oct 2026
 */

#include "upload.hpp"

#include "remote_config.hpp"

#include <array>
#include <charconv>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <zlib.h>

#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <csignal>
#include <pwd.h>
#include <unistd.h>
#endif

namespace log_server {

namespace {

namespace fs = std::filesystem;

constexpr long connect_timeout_s = 10;
constexpr long token_timeout_s = 30;
/// slower than 1 KB/s for this long is given up on
constexpr long stall_limit_s = 60;

struct TempFile {
	fs::path path;
	~TempFile() {
		std::error_code ec;
		fs::remove(path, ec);
	}
};

struct CurlHandle {
	CURL* handle = curl_easy_init();
	~CurlHandle() { curl_easy_cleanup(handle); }
};

struct SlistGuard {
	curl_slist* list = nullptr;
	~SlistGuard() { curl_slist_free_all(list); }
};

struct MimeGuard {
	curl_mime* mime = nullptr;
	~MimeGuard() { curl_mime_free(mime); }
};

std::size_t appendToString(char* data, std::size_t size, std::size_t count, void* user) {
	static_cast<std::string*>(user)->append(data, size * count);
	return size * count;
}

void setCommon(CURL* handle, const std::string& url) {
	curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, connect_timeout_s);
	curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
}

std::optional<std::string> fetchToken(const std::string& base) {
	CurlHandle curl;
	if (curl.handle == nullptr) {
		return std::nullopt;
	}
	std::string body;
	setCommon(curl.handle, base + "/kenzo/token");
	curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT, token_timeout_s);
	curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, appendToString);
	curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &body);
	if (curl_easy_perform(curl.handle) != CURLE_OK) {
		return std::nullopt;
	}
	long status = 0;
	curl_easy_getinfo(curl.handle, CURLINFO_RESPONSE_CODE, &status);
	if (status != 200) {
		return std::nullopt;
	}
	const auto json = nlohmann::json::parse(body, nullptr, false);
	if (!json.is_object() || !json.contains("token") || !json["token"].is_string()) {
		return std::nullopt;
	}
	return json["token"].get<std::string>();
}

/// the compressed file as a curl mime part
struct FileSource {
	std::ifstream in;
	curl_off_t size = 0;
};

std::size_t readFileSource(char* buffer, std::size_t size, std::size_t count, void* user) {
	auto* source = static_cast<FileSource*>(user);
	source->in.read(buffer, static_cast<std::streamsize>(size * count));
	return static_cast<std::size_t>(source->in.gcount());
}

int seekFileSource(void* user, curl_off_t offset, int origin) {
	auto* source = static_cast<FileSource*>(user);
	source->in.clear();
	const auto dir = origin == SEEK_SET ? std::ios::beg : origin == SEEK_CUR ? std::ios::cur : std::ios::end;
	source->in.seekg(static_cast<std::streamoff>(offset), dir);
	return source->in ? CURL_SEEKFUNC_OK : CURL_SEEKFUNC_CANTSEEK;
}

bool postSession(const std::string& base, const std::string& token, const UploadJob& job, const fs::path& gz) {
	FileSource source;
	source.in.open(gz, std::ios::binary);
	if (!source.in) {
		return false;
	}
	std::error_code ec;
	source.size = static_cast<curl_off_t>(fs::file_size(gz, ec));
	if (ec) {
		return false;
	}

	CurlHandle curl;
	if (curl.handle == nullptr) {
		return false;
	}
	MimeGuard mime;
	mime.mime = curl_mime_init(curl.handle);
	const auto text = [&](const char* name, const std::string& value) {
		curl_mimepart* part = curl_mime_addpart(mime.mime);
		curl_mime_name(part, name);
		curl_mime_data(part, value.data(), value.size());
	};
	text("host", job.host);
	text("user", job.user);
	text("started", job.started);
	text("duration_s", std::to_string(job.duration_s));
	text("size", std::to_string(job.size));
	curl_mimepart* file = curl_mime_addpart(mime.mime);
	curl_mime_name(file, "file");
	curl_mime_filename(file, (toUtf8(job.csv.stem()) + ".csv.gz").c_str());
	curl_mime_type(file, "application/gzip");
	curl_mime_data_cb(file, source.size, readFileSource, seekFileSource, nullptr, &source);

	SlistGuard headers;
	headers.list = curl_slist_append(headers.list, ("Authorization: Bearer " + token).c_str());
	// skip the Expect 100-continue round trip
	headers.list = curl_slist_append(headers.list, "Expect:");

	std::string response;
	setCommon(curl.handle, base + "/api/collections/logs/records");
	curl_easy_setopt(curl.handle, CURLOPT_HTTPHEADER, headers.list);
	curl_easy_setopt(curl.handle, CURLOPT_MIMEPOST, mime.mime);
	curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT, g_upload_timeout_seconds);
	curl_easy_setopt(curl.handle, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(curl.handle, CURLOPT_LOW_SPEED_TIME, stall_limit_s);
	curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, appendToString);
	curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &response);
	if (curl_easy_perform(curl.handle) != CURLE_OK) {
		return false;
	}
	long status = 0;
	curl_easy_getinfo(curl.handle, CURLINFO_RESPONSE_CODE, &status);
	return status >= 200 && status < 300;
}

#ifdef _WIN32
std::string narrow(const std::wstring& wide) {
	if (wide.empty()) {
		return {};
	}
	const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<std::size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size, nullptr, nullptr);
	return out;
}
#endif

}  // namespace

std::string formatUtc(std::chrono::system_clock::time_point time) {
	using namespace std::chrono;
	const auto millis = duration_cast<milliseconds>(time.time_since_epoch());
	const std::time_t seconds = static_cast<std::time_t>(floor<std::chrono::seconds>(millis).count());
	std::tm utc {};
#ifdef _WIN32
	gmtime_s(&utc, &seconds);
#else
	gmtime_r(&seconds, &utc);
#endif
	const auto fraction = static_cast<int>((millis - floor<std::chrono::seconds>(millis)).count());
	std::array<char, 40> text {};
	std::snprintf(text.data(), text.size(), "%04d-%02d-%02d %02d:%02d:%02d.%03dZ", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
	              utc.tm_hour, utc.tm_min, utc.tm_sec, fraction);
	return text.data();
}

std::string hostName() {
	std::string name;
#ifdef _WIN32
	DWORD size = 0;
	GetComputerNameExW(ComputerNameDnsHostname, nullptr, &size);
	if (size > 1) {
		std::wstring wide(size, L'\0');
		if (GetComputerNameExW(ComputerNameDnsHostname, wide.data(), &size)) {
			wide.resize(size);
			name = narrow(wide);
		}
	}
#else
	std::array<char, 256> buffer {};
	if (gethostname(buffer.data(), buffer.size() - 1) == 0) {
		name = buffer.data();
	}
#endif
	return name.empty() ? "unknown" : name;
}

std::string userName() {
	std::string name;
#ifdef _WIN32
	DWORD size = 0;
	GetUserNameW(nullptr, &size);
	if (size > 1) {
		std::wstring wide(size, L'\0');
		if (GetUserNameW(wide.data(), &size)) {
			wide.resize(size > 0 ? size - 1 : 0);
			name = narrow(wide);
		}
	}
#else
	if (const passwd* entry = getpwuid(getuid()); entry != nullptr && entry->pw_name != nullptr) {
		name = entry->pw_name;
	}
#endif
	return name.empty() ? "unknown" : name;
}

bool gzipFile(const fs::path& in_path, const fs::path& out_path) {
	std::ifstream in(in_path, std::ios::binary);
	std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
	if (!in || !out) {
		return false;
	}
	z_stream stream {};
	if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
		return false;
	}
	std::array<char, 65536> input {};
	std::array<char, 65536> output {};
	bool ok = true;
	bool done = false;
	while (ok && !done) {
		in.read(input.data(), static_cast<std::streamsize>(input.size()));
		const auto got = in.gcount();
		if (!in.eof() && !in) {
			ok = false;
			break;
		}
		const int flush = in.eof() ? Z_FINISH : Z_NO_FLUSH;
		stream.next_in = reinterpret_cast<Bytef*>(input.data());
		stream.avail_in = static_cast<uInt>(got);
		do {
			stream.next_out = reinterpret_cast<Bytef*>(output.data());
			stream.avail_out = static_cast<uInt>(output.size());
			const int status = deflate(&stream, flush);
			if (status == Z_STREAM_ERROR) {
				ok = false;
				break;
			}
			out.write(output.data(), static_cast<std::streamsize>(output.size() - stream.avail_out));
			if (!out) {
				ok = false;
				break;
			}
		} while (stream.avail_out == 0);
		done = flush == Z_FINISH;
	}
	deflateEnd(&stream);
	out.close();
	return ok && !out.fail();
}

void runUpload(const UploadJob& job) {
	try {
		static const bool curl_ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
		if (!curl_ready) {
			return;
		}
		const std::string base = remoteBaseUrl();
		if (base.empty()) {
			return;
		}
		TempFile gz {fs::path(job.csv).concat(".gz.tmp")};
		if (!gzipFile(job.csv, gz.path)) {
			return;
		}
		const auto token = fetchToken(base);
		if (!token) {
			return;
		}
		postSession(base, *token, job, gz.path);
	} catch (...) {
		// best effort
	}
}

std::vector<std::string> uploadArgs(const UploadJob& job) {
	return {"--upload",   toUtf8(job.csv),  "--host", job.host, "--user", job.user, "--started", job.started,
	        "--duration", std::to_string(job.duration_s), "--size", std::to_string(job.size)};
}

std::optional<UploadJob> parseUploadArgs(const std::vector<std::string>& args) {
	if (args.size() < 2 || args[0] != "--upload") {
		return std::nullopt;
	}
	UploadJob job;
	job.csv = fs::path(std::u8string(args[1].begin(), args[1].end()));
	bool have_duration = false;
	bool have_size = false;
	for (std::size_t i = 2; i < args.size(); i += 2) {
		if (i + 1 >= args.size()) {
			return std::nullopt;
		}
		const std::string& key = args[i];
		const std::string& value = args[i + 1];
		const auto number = [&](std::uint64_t& out) {
			const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
			return ec == std::errc() && end == value.data() + value.size();
		};
		if (key == "--host") {
			job.host = value;
		} else if (key == "--user") {
			job.user = value;
		} else if (key == "--started") {
			job.started = value;
		} else if (key == "--duration") {
			if (!number(job.duration_s)) {
				return std::nullopt;
			}
			have_duration = true;
		} else if (key == "--size") {
			if (!number(job.size)) {
				return std::nullopt;
			}
			have_size = true;
		} else {
			return std::nullopt;
		}
	}
	if (job.host.empty() || job.user.empty() || job.started.empty() || !have_duration || !have_size) {
		return std::nullopt;
	}
	return job;
}

namespace {

#ifdef _WIN32
std::wstring widen(const std::string& text) {
	if (text.empty()) {
		return {};
	}
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<std::size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
	return out;
}

/// quoting as CommandLineToArgvW reads it back
std::wstring quoteArg(const std::wstring& arg) {
	if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
		return arg;
	}
	std::wstring out = L"\"";
	for (auto it = arg.begin();; ++it) {
		std::size_t backslashes = 0;
		while (it != arg.end() && *it == L'\\') {
			++it;
			++backslashes;
		}
		if (it == arg.end()) {
			out.append(backslashes * 2, L'\\');
			break;
		}
		if (*it == L'"') {
			out.append(backslashes * 2 + 1, L'\\');
		} else {
			out.append(backslashes, L'\\');
		}
		out.push_back(*it);
	}
	out.push_back(L'"');
	return out;
}

bool spawnDetached(const std::vector<std::string>& args) {
	std::wstring exe(MAX_PATH, L'\0');
	for (;;) {
		const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
		if (length == 0) {
			return false;
		}
		if (length < exe.size()) {
			exe.resize(length);
			break;
		}
		exe.resize(exe.size() * 2);
	}
	std::wstring command = quoteArg(exe);
	for (const auto& arg : args) {
		command += L' ' + quoteArg(widen(arg));
	}
	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process {};
	const DWORD flags = DETACHED_PROCESS | CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT;
	if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &startup, &process)) {
		return false;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
}
#else
bool spawnDetached(const std::vector<std::string>& args) {
	std::error_code ec;
	const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
	if (ec) {
		return false;
	}
	std::vector<std::string> storage;
	storage.push_back(exe.string());
	storage.insert(storage.end(), args.begin(), args.end());
	std::vector<char*> argv;
	for (auto& arg : storage) {
		argv.push_back(arg.data());
	}
	argv.push_back(nullptr);

	// no zombies when running with --infinite
	signal(SIGCHLD, SIG_IGN);
	const pid_t child = fork();
	if (child < 0) {
		return false;
	}
	if (child == 0) {
		setsid();
		// sockets must not outlive the server
		const long limit = sysconf(_SC_OPEN_MAX);
		for (int fd = 3; fd < (limit > 0 && limit < 65536 ? limit : 65536); ++fd) {
			close(fd);
		}
		const int null = open("/dev/null", O_RDWR);
		if (null >= 0) {
			dup2(null, 0);
			dup2(null, 1);
			dup2(null, 2);
		}
		execv(argv[0], argv.data());
		_exit(0);
	}
	return true;
}
#endif

}  // namespace

void spawnUploader(const SessionSummary& summary) {
	try {
		if (const char* off = std::getenv("KENZO_NO_UPLOAD"); off != nullptr && std::string_view(off) == "1") {
			return;
		}
		std::error_code ec;
		if (summary.size == 0 || summary.path.empty() || !fs::exists(summary.path, ec)) {
			return;
		}
		if (remoteBaseUrl().empty()) {
			return;
		}
		UploadJob job;
		job.csv = summary.path;
		job.host = hostName();
		job.user = userName();
		job.started = formatUtc(summary.started);
		job.duration_s = static_cast<std::uint64_t>(summary.duration.count());
		job.size = summary.size;
		const auto args = uploadArgs(job);
		if (g_spawn_override) {
			g_spawn_override(args);
		} else {
			spawnDetached(args);
		}
	} catch (...) {
		// never get in the way of shutting down
	}
}

void finishAndUpload(LogStorage& storage) {
	if (const auto summary = storage.finishSession()) {
		spawnUploader(*summary);
	}
}

}  // namespace log_server
