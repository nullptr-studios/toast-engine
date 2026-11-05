/**
 * @file pack.hpp
 * @author Xein
 * @date 8 Jul 2026
 * @brief Toat pack reader
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <toast_pack/pack.hpp>
#include <vector>

namespace assets {

class PackArchive {
public:
	explicit PackArchive(const std::filesystem::path& path);

	[[nodiscard]]
	auto read(std::string_view rel_path) const -> std::optional<std::vector<uint8_t>>;

	[[nodiscard]]
	auto path() const -> const std::filesystem::path& {
		return m_reader.path();
	}

	[[nodiscard]]
	auto entryCount() const -> std::size_t {
		return m_reader.entryCount();
	}

private:
	pack::PackReader m_reader;
};

}
