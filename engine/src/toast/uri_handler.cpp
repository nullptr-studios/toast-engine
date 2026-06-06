#include "uri_handler.hpp"

#include "ffi/engine.h"

#include <filesystem>

namespace toast {
auto setAssetsPath(std::string_view path) -> void {
	TOAST_INFO("ResourceManager", "Assets URI changed to {}", path);
	_detail::assets_path = path;
}

auto setArtworkPath(std::string_view path) -> void {
	TOAST_INFO("ResourceManager", "Artwork URI changed to {}", path);
	_detail::artworks_path = path;
}

auto setCachePath(std::string_view path) -> void {
	TOAST_INFO("ResourceManager", "Cache URI changed to {}", path);
	_detail::cache_path = path;
}

auto setCorePath(std::string_view path) -> void {
	TOAST_INFO("ResourceManager", "Core URI changed to {}", path);
	_detail::core_path = path;
}

auto handleURI(std::string_view path) -> std::pair<URI, std::filesystem::path> {
	if (path.starts_with(_detail::asset_uri)) {
		auto p = _detail::assets_path / std::string(path.substr(_detail::asset_uri.size()));
		return {URI::asset, p};
	}
	if (path.starts_with(_detail::artwork_uri)) {
		auto p = _detail::artworks_path / std::string(path.substr(_detail::artwork_uri.size()));
		return {URI::artwork, p};
	}
	if (path.starts_with(_detail::cache_uri)) {
		return {URI::cache, std::string {path.substr(_detail::cache_uri.size())}};
	}
	if (path.starts_with(_detail::core_uri)) {
		return {URI::core, std::string {path.substr(_detail::core_uri.size())}};
	}
	if (path.starts_with(_detail::node_uri)) {
		return {URI::node, std::string {path.substr(_detail::node_uri.size())}};
	}

	TOAST_WARN("ResourceManager", "Path {} did not contain a URI", path);
	return {URI::null, std::string {path}};
}

auto setUseToastPack(bool use) -> void {
	_detail::uses_toast_pack = use;
}
}

extern "C" {

void uri_set_working_directory(const char* project, const char* engine) {
	auto path = std::filesystem::path(project);
	auto engine_path = std::filesystem::path(engine);

	toast::setAssetsPath((path / "assets").string());
	toast::setArtworkPath((path / "artwork").string());
	toast::setCachePath((path / ".toast").string());
	toast::setCorePath((engine_path / "assets").string());
}
}
