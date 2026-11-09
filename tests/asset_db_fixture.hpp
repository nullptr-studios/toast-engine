#pragma once

#include <filesystem>
#include <initializer_list>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace test_support {

struct AssetRow {
	std::string uid;
	std::string type;
	std::string db;
	std::string path;
};

struct PakRow {
	std::string db;
	std::string file;
};

inline void writeAssetDb(const std::filesystem::path& file, const std::vector<AssetRow>& assets, const std::vector<PakRow>& paks = {}) {
	std::filesystem::create_directories(file.parent_path());
	std::filesystem::remove(file);

	sqlite3* db = nullptr;
	const auto utf8 = file.generic_string();
	if (sqlite3_open(utf8.c_str(), &db) != SQLITE_OK) {
		throw std::runtime_error("cannot create " + utf8);
	}

	auto exec = [&](const std::string& sql) {
		char* err = nullptr;
		if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
			const std::string message = err ? err : "unknown";
			sqlite3_free(err);
			sqlite3_close(db);
			throw std::runtime_error("sqlite: " + message);
		}
	};
	auto quote = [](const std::string& value) {
		std::string out = "'";
		for (const char c : value) {
			out += c == '\'' ? "''" : std::string(1, c);
		}
		return out + "'";
	};

	exec("CREATE TABLE assets (uid TEXT PRIMARY KEY, type TEXT NOT NULL, db TEXT NOT NULL, path TEXT NOT NULL)");
	for (const auto& row : assets) {
		exec("INSERT INTO assets VALUES (" + quote(row.uid) + "," + quote(row.type) + "," + quote(row.db) + "," + quote(row.path) + ")");
	}
	if (!paks.empty()) {
		exec("CREATE TABLE paks (db TEXT PRIMARY KEY, file TEXT NOT NULL)");
		for (const auto& pak : paks) {
			exec("INSERT INTO paks VALUES (" + quote(pak.db) + "," + quote(pak.file) + ")");
		}
	}
	sqlite3_close(db);
}

}
