#include "migrate.hpp"

#include "files.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <toast/log.hpp>
#include <toast/physics/physics_settings.hpp>
#include <vector>

namespace toast::params {
namespace {

auto readAll(const std::filesystem::path& path) -> std::string {
	std::ifstream in(path, std::ios::binary);
	std::stringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

void mergeMissing(Document& target, const Document& source) {
	for (const auto& [key, value] : source.items()) {
		const auto it = target.find(key);
		if (it == target.end()) {
			target[key] = value;
		} else if (it->is_object() && value.is_object()) {
			mergeMissing(*it, value);
		}
	}
}

auto writeGroups(const std::filesystem::path& directory, const std::map<std::string, Document>& groups) -> bool {
	DirectoryConfigSource out {directory};
	bool ok = true;
	for (const auto& [group, moved] : groups) {
		Document merged = moved;
		if (auto existing = out.read(group); existing.status == ReadStatus::ok) {
			merged = std::move(existing.document);
			mergeMissing(merged, moved);
		}
		ok = out.write(group, merged) && ok;
	}
	return ok;
}

auto relocate(
    Document& doc, std::string_view from_table, std::string_view key, std::string_view to_table, std::string_view to_key
) -> bool {
	const auto from = doc.find(from_table);
	if (from == doc.end() || !from->is_object() || !from->contains(key)) {
		return false;
	}
	Document& table = doc[std::string {to_table}];
	if (!table.is_object()) {
		table = Document::object();
	}
	if (!table.contains(to_key)) {
		table[std::string {to_key}] = (*from)[std::string {key}];
	}
	from->erase(std::string {key});
	if (from->empty()) {
		doc.erase(std::string {from_table});
	}
	return true;
}

auto joinComponents(Document& table, const std::string& name, std::array<double, 3> fallback) -> bool {
	static constexpr std::array<const char*, 3> k_axes {"_x", "_y", "_z"};
	bool any = false;
	std::array<double, 3> values = fallback;
	for (size_t i = 0; i < 3; ++i) {
		const std::string key = name + k_axes[i];
		if (const auto it = table.find(key); it != table.end()) {
			any = true;
			if (it->is_number()) {
				values[i] = it->get<double>();
			}
			table.erase(key);
		}
	}
	if (!any) {
		return false;
	}
	if (!table.contains(name)) {
		table[name] = Document::array({values[0], values[1], values[2]});
	}
	return true;
}

auto isLayerIndex(const std::string& key) -> bool {
	return !key.empty() && std::ranges::all_of(key, [](unsigned char c) { return std::isdigit(c) != 0; });
}

auto upgradePhysics(Document& doc) -> bool {
	bool changed = false;

	if (const auto layers = doc.find("layers"); layers != doc.end() && layers->is_object()) {
		std::vector<std::string> numbered;
		for (const auto& [key, value] : layers->items()) {
			if (isLayerIndex(key) && value.is_object()) {
				numbered.push_back(key);
			}
		}
		if (!numbered.empty()) {
			const bool had_names = layers->contains("names") && (*layers)["names"].is_array();
			const bool had_abbreviations = layers->contains("abbreviations") && (*layers)["abbreviations"].is_array();
			Document names = had_names ? (*layers)["names"] : Document(physics::defaultLayerNames());
			Document abbreviations = had_abbreviations ? (*layers)["abbreviations"] : Document(physics::defaultLayerAbbreviations());
			for (const auto& key : numbered) {
				const size_t index = std::stoul(key);
				const Document& layer = (*layers)[key];
				if (index < names.size() && !had_names && layer.contains("name") && layer["name"].is_string()) {
					names[index] = layer["name"];
				}
				if (index < abbreviations.size() && !had_abbreviations && layer.contains("abbr") && layer["abbr"].is_string()) {
					abbreviations[index] = layer["abbr"];
				}
				layers->erase(key);
			}
			(*layers)["names"] = std::move(names);
			(*layers)["abbreviations"] = std::move(abbreviations);
			changed = true;
		}
	}

	if (const auto world = doc.find("world"); world != doc.end() && world->is_object()) {
		changed = joinComponents(*world, "gravity", {0.0, 0.0, -9.8}) || changed;
		changed = joinComponents(*world, "wind", {0.0, 0.0, 0.0}) || changed;
	}

	changed = relocate(doc, "debug", "time_narrow_pairs", "step", "time_narrow_pairs") || changed;
	changed = relocate(doc, "debug", "draw_shapes", "world", "draw_shapes") || changed;
	changed = relocate(doc, "debug", "draw_bounds", "broadphase", "draw_bounds") || changed;
	changed = relocate(doc, "debug", "draw_contacts", "solver", "draw_contacts") || changed;
	changed = relocate(doc, "debug", "draw_velocity", "world", "draw_velocity") || changed;
	changed = relocate(doc, "debug", "draw_centers", "world", "draw_centers") || changed;
	changed = relocate(doc, "debug", "velocity_scale", "world", "velocity_scale") || changed;
	changed = relocate(doc, "debug", "normal_length", "solver", "contact_normal_length") || changed;
	return changed;
}

auto upgradeRenderer(Document& doc) -> bool {
	bool changed = relocate(doc, "debug", "overlay_in_game", "display", "debug_overlay");
	changed = relocate(doc, "debug", "glass_cost_view", "voxels", "glass_cost_view") || changed;
	return changed;
}

}

auto upgradeLegacyConfig(const std::filesystem::path& config_dir) -> bool {
	DirectoryConfigSource source {config_dir};
	bool any = false;
	const auto upgrade = [&](const char* group, bool (*fix)(Document&)) {
		auto read = source.read(group);
		if (read.status != ReadStatus::ok || !fix(read.document)) {
			return;
		}
		if (source.write(group, read.document)) {
			TOAST_INFO("Params", "Brought {}.toast in {} up to the current parameters", group, config_dir.string());
			any = true;
		}
	};
	upgrade("physics", upgradePhysics);
	upgrade("renderer", upgradeRenderer);
	return any;
}

auto migrateProjectToast(const std::filesystem::path& toast_file, const std::filesystem::path& config_dir) -> bool {
	std::error_code ec;
	if (!std::filesystem::is_regular_file(toast_file, ec)) {
		return false;
	}
	auto parsed = tomlToDocument(readAll(toast_file));
	if (!parsed) {
		TOAST_WARN("Params", "{} could not be read, so it was not migrated: {}", toast_file.string(), parsed.error());
		return false;
	}

	Document kept = Document::object();
	std::map<std::string, Document> groups;
	for (const auto& [key, value] : parsed->items()) {
		if (key == "name" || key == "version") {
			kept[key] = value;
		} else if (key == "databases" || key == "max_tick_rate") {
			groups.try_emplace("project", Document::object()).first->second[key] = value;
		} else if (value.is_object()) {
			groups[key] = value;
		} else {
			kept[key] = value;
		}
	}
	if (groups.empty()) {
		return false;
	}

	if (!writeGroups(config_dir, groups)) {
		TOAST_ERROR(
		    "Params", "Could not write the config files in {}; {} was left as it was", config_dir.string(), toast_file.string()
		);
		return false;
	}

	{
		auto temporary = toast_file;
		temporary += ".tmp";
		std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
		out << documentToToml(kept);
		out.close();
		std::filesystem::rename(temporary, toast_file, ec);
		if (ec) {
			TOAST_ERROR("Params", "Could not rewrite {}: {}", toast_file.string(), ec.message());
			return false;
		}
	}
	TOAST_INFO("Params", "Moved the settings in {} to {}", toast_file.string(), config_dir.string());
	return true;
}

auto migrateLegacyUserSettings(const std::filesystem::path& old_file, const std::filesystem::path& saved_config_dir) -> bool {
	std::error_code ec;
	if (!std::filesystem::is_regular_file(old_file, ec)) {
		return false;
	}
	auto parsed = tomlToDocument(readAll(old_file));
	if (!parsed) {
		TOAST_WARN("Params", "{} could not be read, so it was not migrated: {}", old_file.string(), parsed.error());
		return false;
	}

	std::map<std::string, Document> groups;
	for (const auto& [key, value] : parsed->items()) {
		if (value.is_object()) {
			groups[key] = value;
		}
	}
	if (!groups.empty() && !writeGroups(saved_config_dir, groups)) {
		TOAST_ERROR(
		    "Params", "Could not write the config files in {}; {} was left as it was", saved_config_dir.string(), old_file.string()
		);
		return false;
	}

	auto migrated = old_file;
	migrated += ".migrated";
	std::filesystem::rename(old_file, migrated, ec);
	if (ec) {
		TOAST_ERROR("Params", "Could not set {} aside: {}", old_file.string(), ec.message());
		return false;
	}
	return true;
}

}
