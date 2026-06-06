/**
 * @file uri_handler.hpp
 * @author Xein
 * @date 22 May 2026
 *
 * @brief Handles URI parsing and path resolution for asset, artwork, and node protocols.
 */

#pragma once
#include "log.hpp"

namespace toast {

enum class URI : uint8_t {
	null,
	asset,
	artwork,
	node,
	cache,
	core,
};

namespace _detail {
constexpr std::string_view asset_uri = "asset://";
constexpr std::string_view artwork_uri = "artwork://";
constexpr std::string_view node_uri = "node://";
constexpr std::string_view cache_uri = "cache://";
constexpr std::string_view core_uri = "core://";

inline bool uses_toast_pack = false;
inline std::filesystem::path assets_path;
inline std::filesystem::path artworks_path;
inline std::filesystem::path cache_path;
inline std::filesystem::path core_path;
}

void setAssetsPath(std::string_view path);
void setArtworkPath(std::string_view path);
void setCachePath(std::string_view path);
void setCorePath(std::string_view path);
void setUseToastPack(bool use);
auto handleURI(std::string_view path) -> std::pair<URI, std::filesystem::path>;

}
