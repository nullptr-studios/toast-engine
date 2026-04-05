#include "Toast/Localization.hpp"

#include "Toast/GameEvents.hpp"
#include "Toast/Log.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace toast {
namespace {
using Table = std::unordered_map<std::string, std::string>;

std::unordered_map<std::string, Table> s_tables;
std::string s_language = "en";

static std::string Trim(std::string value) {
	const auto begin = value.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos) {
		return {};
	}
	const auto end = value.find_last_not_of(" \t\r\n");
	return value.substr(begin, end - begin + 1);
}

static std::string NormalizeLanguageCode(std::string code) {
	code = Trim(std::move(code));

	if (code.size() >= 2 && ((code.front() == '"' && code.back() == '"') || (code.front() == '\'' && code.back() == '\''))) {
		code = code.substr(1, code.size() - 2);
	}

	std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});

	if (code == "english") {
		return "en";
	}
	if (code == "spanish" || code == "espanol") {
		return "es";
	}
	if (code == "romanian") {
		return "ro";
	}
	if (code == "chinese") {
		return "zh";
	}
	if (code == "russian") {
		return "ru";
	}
	if (code == "meow") {
		return "cat";
	}

	return code;
}

bool EnsureLocalizationLoaded() {
	if (!s_tables.empty()) {
		return true;
	}

	// Fallback to both common resource roots so localization works in game/editor flows.
	if (Localization::LoadFile("UI/locales.json")) {
		return true;
	}
	if (Localization::LoadFile("assets/UI/locales.json")) {
		return true;
	}

	return false;
}

static std::string EscapeJSString(std::string_view value) {
	std::string out;
	out.reserve(value.size());
	for (char c : value) {
		switch (c) {
			case '\\': out += "\\\\"; break;
			case '\'': out += "\\'"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default: out += c; break;
		}
	}
	return out;
}

static nlohmann::json CurrentDictionaryJson() {
	nlohmann::json dict = nlohmann::json::object();
	const auto table_it = s_tables.find(s_language);
	if (table_it == s_tables.end()) {
		return dict;
	}

	for (const auto& [key, value] : table_it->second) {
		dict[key] = value;
	}
	return dict;
}
}    // namespace

bool Localization::LoadFile(const std::string& resource_path) {
	auto raw = resource::Open(resource_path);
	if (!raw.has_value()) {
		TOAST_ERROR("Localization file not found: {}", resource_path);
		return false;
	}

	nlohmann::json root;
	try {
		std::istringstream stream(raw.value());
		stream >> root;
	} catch (const std::exception& e) {
		TOAST_ERROR("Failed to parse localization file {}: {}", resource_path, e.what());
		return false;
	}

	if (!root.is_object()) {
		TOAST_ERROR("Localization file {} must be a JSON object", resource_path);
		return false;
	}

	s_tables.clear();
	for (auto it = root.begin(); it != root.end(); ++it) {
		if (!it.value().is_object()) {
			continue;
		}

		const std::string lang = NormalizeLanguageCode(it.key());
		if (lang.empty()) {
			continue;
		}

		Table table;
		for (auto jt = it.value().begin(); jt != it.value().end(); ++jt) {
			if (jt.value().is_string()) {
				table[jt.key()] = jt.value().get<std::string>();
			}
		}
		s_tables[lang] = std::move(table);
	}

	if (s_tables.empty()) {
		TOAST_ERROR("Localization file {} does not contain language tables", resource_path);
		return false;
	}

	if (!s_tables.contains(s_language)) {
		s_language = s_tables.begin()->first;
	}

	TOAST_INFO("Localization loaded {} languages from {}", s_tables.size(), resource_path);
	return true;
}

bool Localization::SetLanguage(const std::string& language_code) {
	if (!EnsureLocalizationLoaded()) {
		TOAST_WARN("Localization tables are empty; cannot set language {}", language_code);
		return false;
	}

	const std::string normalized = NormalizeLanguageCode(language_code);
	if (!s_tables.contains(normalized)) {
		TOAST_WARN("Localization language not available: {} (normalized: {})", language_code, normalized);
		return false;
	}
	if (s_language == normalized) {
		return true;
	}

	s_language = normalized;
	event::Send(new toast::LocalizationChanged(s_language));
	TOAST_INFO("Localization language set to {}", s_language);
	return true;
}

const std::string& Localization::GetLanguage() {
	return s_language;
}

std::string Localization::Translate(std::string_view key) {
	if (!EnsureLocalizationLoaded()) {
		return std::string(key);
	}

	const auto table_it = s_tables.find(s_language);
	if (table_it == s_tables.end()) {
		return std::string(key);
	}

	const auto value_it = table_it->second.find(std::string(key));
	if (value_it == table_it->second.end()) {
		return std::string(key);
	}

	return value_it->second;
}

std::vector<std::string> Localization::GetLanguages() {
	if (!EnsureLocalizationLoaded()) {
		return {};
	}

	std::vector<std::string> out;
	out.reserve(s_tables.size());
	for (const auto& [lang, _] : s_tables) {
		out.push_back(lang);
	}
	return out;
}

std::string Localization::BuildApplyScript() {
	EnsureLocalizationLoaded();

	const std::string lang = EscapeJSString(s_language);
	const std::string dict = CurrentDictionaryJson().dump();

	return "(function(){"
	       "window.__toastLang='" +
	       lang +
	       "';"
	       "window.__toastDict=" +
	       dict +
	       ";"
	       "if(typeof window.onToastLanguageChanged==='function'){window.onToastLanguageChanged(window.__toastLang, window.__toastDict);}"
	       "if(typeof window.applyLocalization==='function'){window.applyLocalization();}"
	       "})();";
}

}    // namespace toast

