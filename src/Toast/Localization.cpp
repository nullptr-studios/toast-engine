#include "Toast/Localization.hpp"

#include "Toast/GameEvents.hpp"
#include "Toast/Log.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <nlohmann/json.hpp>
#include <unordered_map>

namespace toast {
namespace {
using Table = std::unordered_map<std::string, std::string>;

std::unordered_map<std::string, Table> s_tables;
std::string s_language = "en";

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
		root = nlohmann::json::parse(*raw);
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

		Table table;
		for (auto jt = it.value().begin(); jt != it.value().end(); ++jt) {
			if (jt.value().is_string()) {
				table[jt.key()] = jt.value().get<std::string>();
			}
		}
		s_tables[it.key()] = std::move(table);
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
	if (!s_tables.contains(language_code)) {
		TOAST_WARN("Localization language not available: {}", language_code);
		return false;
	}
	if (s_language == language_code) {
		return true;
	}

	s_language = language_code;
	event::Send(new toast::LocalizationChanged(s_language));
	TOAST_INFO("Localization language set to {}", s_language);
	return true;
}

const std::string& Localization::GetLanguage() {
	return s_language;
}

std::string Localization::Translate(std::string_view key) {
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
	std::vector<std::string> out;
	out.reserve(s_tables.size());
	for (const auto& [lang, _] : s_tables) {
		out.push_back(lang);
	}
	return out;
}

std::string Localization::BuildApplyScript() {
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

