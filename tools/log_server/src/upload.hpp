/**
 * @file upload.hpp
 * @author Xein
 * @date 25 Oct 2026
 * @brief Upload of a finished session
 */

#pragma once

#include "storage.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace log_server {

struct UploadJob {
	std::filesystem::path csv;
	std::string host;
	std::string user;
	std::string started;
	std::uint64_t duration_s = 0;
	/// bytes of the csv
	std::uint64_t size = 0;
};

/// utc timestamp as PocketBase prints it
std::string formatUtc(std::chrono::system_clock::time_point time);

/// machine and account names
std::string hostName();
std::string userName();

/// gzips in into out
bool gzipFile(const std::filesystem::path& in, const std::filesystem::path& out);

/// seconds the upload may take
inline long g_upload_timeout_seconds = 300;

/// gzips the csv and posts the session
void runUpload(const UploadJob& job);

/// arguments that make log_server upload a job
std::vector<std::string> uploadArgs(const UploadJob& job);

/// inverse of uploadArgs
std::optional<UploadJob> parseUploadArgs(const std::vector<std::string>& args);

/// replaces the real spawn for the checks
inline std::function<bool(const std::vector<std::string>&)> g_spawn_override;

/// starts a detached upload for the finished session
void spawnUploader(const SessionSummary& summary);

/// finishes the session then spawns the uploader
void finishAndUpload(LogStorage& storage);

}  // namespace log_server
