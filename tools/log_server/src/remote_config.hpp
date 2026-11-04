/**
 * @file remote_config.hpp
 * @author Xein
 * @date 25 Oct 2026
 * @brief Where finished sessions are uploaded
 */

#pragma once

#include <cstdlib>
#include <string>

namespace log_server {

/// KENZO_REMOTE_URL overrides it
inline std::string remoteBaseUrl() {
	std::string url = "http://kenzo.nullptr.es:8080";
	if (const char* env = std::getenv("KENZO_REMOTE_URL"); env != nullptr && *env != '\0') {
		url = env;
	}
	while (!url.empty() && url.back() == '/') {
		url.pop_back();
	}
	return url;
}

}  // namespace log_server
