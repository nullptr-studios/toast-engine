/**
 * @file ResourceManager.hpp
 * @author Xein
 * @date 23 May 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include <future>

namespace toast {

class ResourceManager {
	using BinaryFile = std::optional<std::vector<uint8_t>>;

public:
	ResourceManager() = default;
	~ResourceManager() = default;

	auto loadFile(std::string_view path) -> BinaryFile;
	auto loadFileAsync(std::string_view path) -> std::future<BinaryFile>;

private:
};

}
