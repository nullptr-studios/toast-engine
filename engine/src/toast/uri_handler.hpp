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

inline std::string assets_path;
inline std::string artworks_path;
inline std::string cache_path;
inline std::string core_path;
}

inline auto setAssetsPath(std::string_view path) -> void {
	TOAST_INFO("ResourceManager", "Assets URI changed to {}", path);
	_detail::assets_path = path;
}

inline auto setArtworkPath(std::string_view path) -> void {
	TOAST_INFO("ResourceManager", "Artwork URI changed to {}", path);
	_detail::artworks_path = path;
}

inline auto setCachePath(std::string_view path) -> void {
	TOAST_INFO("ResourceManager", "Cache URI changed to {}", path);
	_detail::cache_path = path;
}

inline auto setCorePath(std::string_view path) -> void {
	TOAST_INFO("ResourceManager", "Core URI changed to {}", path);
	_detail::core_path = path;
}

inline auto handleURI(std::string_view path) -> std::pair<URI, std::string> {
	if (path.starts_with(_detail::asset_uri)) {
		std::string p = _detail::assets_path + std::string(path.substr(_detail::asset_uri.size()));
		return {URI::asset, p};
	}
	if (path.starts_with(_detail::artwork_uri)) {
		std::string p = _detail::artworks_path + std::string(path.substr(_detail::artwork_uri.size()));
		return {URI::artwork, p};
	}
	if (path.starts_with(_detail::node_uri)) {
		return {URI::node, std::string {path.substr(_detail::node_uri.size())}};
	}
	if (path.starts_with(_detail::cache_uri)) {
		return {URI::cache, std::string {path.substr(_detail::cache_uri.size())}};
	}
	if (path.starts_with(_detail::core_uri)) {
		return {URI::core, std::string {path.substr(_detail::core_uri.size())}};
	}

	return {URI::null, std::string {path}};
}

}
