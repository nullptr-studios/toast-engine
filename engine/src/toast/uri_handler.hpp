/**
 * @file uri_handler.hpp
 * @author Xein
 * @date 22 May 2026
 *
 * @brief Handles URI parsing and path resolution for asset, artwork, and node protocols.
 */

#pragma once

namespace toast {

enum class URI : uint8_t {
	error,
	asset,
	artwork,
	node,
};

namespace _detail {
constexpr std::string_view asset_uri = "asset://";
constexpr std::string_view artwork_uri = "artwork://";
constexpr std::string_view node_uri = "node://";

inline std::string assets_path;
inline std::string artworks_path;
}

inline auto setAssetsPath(std::string_view path) -> void {
	_detail::assets_path = path;
}

inline auto setArtworkPath(std::string_view path) -> void {
	_detail::artworks_path = path;
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

	return {URI::error, std::string {path}};
}

}
