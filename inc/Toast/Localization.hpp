#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace toast {

// Global localization service backed by game-provided JSON dictionaries.
class Localization final {
public:
	static bool LoadFile(const std::string& resource_path);
	static bool SetLanguage(const std::string& language_code);
	static const std::string& GetLanguage();
	static std::string Translate(std::string_view key);
	static std::vector<std::string> GetLanguages();
	static std::string BuildApplyScript();
};

}

