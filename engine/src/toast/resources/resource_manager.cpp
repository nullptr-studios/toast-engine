#include "resource_manager.hpp"
#include <toast/thread_pool.hpp>
#include <toast/log.hpp>

namespace toast {
auto ResourceManager::loadFile(std::string_view path) -> BinaryFile {
	std::filesystem::path fs_path(path);
	if (!std::filesystem::exists(fs_path) || !std::filesystem::is_regular_file(fs_path)) {
		TOAST_ERROR("ResourceManager", "Tried to load {} but it doesn't exist", fs_path.string());
		return std::nullopt;
	}

	uintmax_t size = std::filesystem::file_size(fs_path);
	std::ifstream file(fs_path, std::ios::binary);
	if (not file) {
		return std::nullopt;
	}

	std::vector<uint8_t> buffer(size);
	if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
		return buffer;
	}

	return std::nullopt;
}

auto ResourceManager::loadFileAsync(std::string_view path) -> std::future<BinaryFile> {
	return ThreadPool::push([this, path = std::string{path}] {
		return loadFile(path);
	});
}

}
