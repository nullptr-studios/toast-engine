/**
 * @file localization.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief CSV-backed localization tables
 */

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <toast/assets/core_types.hpp>
#include <toast/export.hpp>
#include <unordered_map>
#include <vector>

namespace ui {
class LocalizationTable;
}

namespace assets {

class TOAST_API LocalizationBase : public Asset {
public:
	explicit LocalizationBase(std::vector<uint8_t> data);

	[[nodiscard]]
	auto languages() const noexcept -> const std::vector<std::string>& {
		return m_languages;
	}

	[[nodiscard]]
	auto ids() const noexcept -> const std::vector<std::string>& {
		return m_ids;
	}

	[[nodiscard]]
	auto has(std::string_view id) const -> bool;

	[[nodiscard]]
	auto cell(std::string_view id, std::string_view language) const -> std::optional<std::string>;

protected:
	std::vector<std::string> m_languages;
	std::vector<std::string> m_ids;
	std::unordered_map<std::string, std::vector<std::string>> m_rows;

	[[nodiscard]]
	auto languageIndex(std::string_view language) const -> int;
};

class TOAST_API Localization final : public LocalizationBase {
public:
	using LocalizationBase::LocalizationBase;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "localization";
	}

	[[nodiscard]]
	auto text(std::string_view id, std::string_view language) const -> std::string;
};

class TOAST_API ImageLocalization final : public LocalizationBase {
public:
	using LocalizationBase::LocalizationBase;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "image_localization";
	}

	[[nodiscard]]
	auto image(std::string_view id, std::string_view language) const -> std::string;
};

}
