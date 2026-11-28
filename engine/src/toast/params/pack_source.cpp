#include "pack_source.hpp"

#include "toast/assets/pack.hpp"

#include <toast/log.hpp>

namespace toast::params {

PackConfigSource::PackConfigSource(const std::filesystem::path& pak) : m_archive(std::make_unique<assets::PackArchive>(pak)) { }

PackConfigSource::~PackConfigSource() = default;

auto PackConfigSource::read(std::string_view group) const -> ReadResult {
	const auto bytes = m_archive->read(std::string {group} + ".bson");
	if (!bytes) {
		return {};
	}
	try {
		auto document = Document::from_bson(*bytes);
		if (!document.is_object()) {
			return {ReadStatus::corrupt, Document::object(), "not a document"};
		}
		return {ReadStatus::ok, std::move(document), {}};
	} catch (const std::exception& error) { return {ReadStatus::corrupt, Document::object(), error.what()}; }
}

auto PackConfigSource::write(std::string_view /*group*/, const Document& /*document*/) -> bool {
	return false;
}

auto openProjectSource(const std::filesystem::path& config_dir, const std::filesystem::path& pak)
    -> std::unique_ptr<ConfigSource> {
	std::error_code ec;
	if (!pak.empty() && std::filesystem::is_regular_file(pak, ec)) {
		try {
			return std::make_unique<PackConfigSource>(pak);
		} catch (const std::exception& error) {
			TOAST_ERROR(
			    "Params", "{} is not a readable pack ({}); reading {} instead", pak.string(), error.what(), config_dir.string()
			);
		}
	}
	return std::make_unique<DirectoryConfigSource>(config_dir);
}

}
