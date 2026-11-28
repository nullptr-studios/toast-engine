/**
 * @file pack_source.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief The committed config layer of a shipped game
 */

#pragma once
#include <filesystem>
#include <memory>
#include <toast/export.hpp>
#include <toast/params/files.hpp>

namespace assets {
class PackArchive;
}

namespace toast::params {

class TOAST_API PackConfigSource final : public ConfigSource {
public:
	explicit PackConfigSource(const std::filesystem::path& pak);
	~PackConfigSource() override;

	PackConfigSource(const PackConfigSource&) = delete;
	auto operator=(const PackConfigSource&) -> PackConfigSource& = delete;

	[[nodiscard]]
	auto read(std::string_view group) const -> ReadResult override;

	auto write(std::string_view group, const Document& document) -> bool override;

private:
	std::unique_ptr<assets::PackArchive> m_archive;
};

TOAST_API auto openProjectSource(const std::filesystem::path& config_dir, const std::filesystem::path& pak)
    -> std::unique_ptr<ConfigSource>;

}
