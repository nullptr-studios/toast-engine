/**
 * @file files.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief Reads and writes a group's layers
 */

#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <toast/params/document.hpp>
#include <toast/params/group.hpp>
#include <utility>

namespace toast::params {

enum class ReadStatus : uint8_t {
	missing,
	corrupt,
	ok
};

struct ReadResult {
	ReadStatus status = ReadStatus::missing;
	Document document;
	std::string error;
};

enum class ConfigFormat : uint8_t {
	toml,
	bson
};

class TOAST_API ConfigSource {
public:
	virtual ~ConfigSource() = default;

	[[nodiscard]]
	virtual auto read(std::string_view group) const -> ReadResult = 0;

	virtual auto write(std::string_view group, const Document& document) -> bool = 0;
};

class TOAST_API DirectoryConfigSource final : public ConfigSource {
public:
	explicit DirectoryConfigSource(std::filesystem::path directory, ConfigFormat format = ConfigFormat::toml);

	[[nodiscard]]
	auto read(std::string_view group) const -> ReadResult override;

	auto write(std::string_view group, const Document& document) -> bool override;

	[[nodiscard]]
	auto directory() const -> const std::filesystem::path& {
		return m_directory;
	}

private:
	[[nodiscard]]
	auto pathFor(std::string_view group) const -> std::filesystem::path;

	std::filesystem::path m_directory;
	ConfigFormat m_format;
};

class TOAST_API Files {
public:
	Files(std::unique_ptr<ConfigSource> project, std::unique_ptr<ConfigSource> saved);

	void load(GroupBase& group, bool keep_unsaved = false);

	auto save(GroupBase& group, Layer layer) -> bool;

	auto saveDirty() -> size_t;

	void reload();

private:
	[[nodiscard]]
	auto source(Layer layer) const -> ConfigSource*;

	std::unique_ptr<ConfigSource> m_project;
	std::unique_ptr<ConfigSource> m_saved;
	std::map<std::pair<std::string, Layer>, Document> m_documents;
	std::set<std::string> m_reported;
};

}
