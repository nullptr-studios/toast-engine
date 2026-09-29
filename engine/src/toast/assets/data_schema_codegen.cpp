#include "data_schema_codegen.hpp"

#include "asset_manager.hpp"
#include "assets.hpp"
#include "schema.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <toast/log.hpp>
#include <unordered_map>
#include <unordered_set>

namespace assets {

namespace {

// bro i feel like ive coded this function in 10 different places now -x
auto pascalCase(std::string_view raw) -> std::string {
	std::string result;
	bool cap_next = true;
	for (char c : raw) {
		if (c == '_' || c == '-' || c == ' ' || c == '.') {
			cap_next = true;
			continue;
		}
		if (cap_next) {
			result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			cap_next = false;
		} else {
			result += c;
		}
	}
	return result;
}

auto rootNameFromPath(std::string_view path) -> std::string {
	auto slash = path.find_last_of("/\\");
	std::string_view filename = slash == std::string_view::npos ? path : path.substr(slash + 1);
	constexpr std::string_view suffix = ".schema.json";
	if (filename.ends_with(suffix)) {
		filename.remove_suffix(suffix.size());
	} else if (auto dot = filename.find('.'); dot != std::string_view::npos) {
		filename = filename.substr(0, dot);
	}
	auto name = pascalCase(filename);
	return name.empty() ? "Unnamed" : name;
}

class Emitter {
public:
	explicit Emitter(std::string root_name) : m_root_name(std::move(root_name)) { }

	void emitRoot(const std::vector<SchemaField>& fields) { emitClass(m_root_name, fields, "Asset"); }

	[[nodiscard]]
	auto text() const -> std::string {
		return m_out.str();
	}

private:
	std::string m_root_name;
	std::ostringstream m_out;
	std::unordered_set<std::string> m_emitted_classes;

	auto luaTypeOf(const SchemaField& field) -> std::string {
		std::string base;

		if (field.type_switch.has_value()) {
			std::vector<std::string> parts;
			for (const auto& c : field.type_switch->cases) {
				SchemaField probe = field;
				probe.type = c.type;
				probe.type_switch.reset();
				auto t = luaTypeOf(probe);
				if (std::ranges::find(parts, t) == parts.end()) {
					parts.push_back(std::move(t));
				}
			}
			base = parts.empty() ? "any" : parts.front();
			for (size_t i = 1; i < parts.size(); ++i) {
				base += "|" + parts[i];
			}
		} else {
			switch (field.type) {
				case DataType::bool_t: base = "boolean"; break;
				case DataType::int_t: base = "integer"; break;
				case DataType::float_t: base = "number"; break;
				case DataType::string_t: base = field.enum_options.empty() ? "string" : enumUnion(field.enum_options); break;
				case DataType::vec2_t: base = "vec2"; break;
				case DataType::vec3_t: base = "vec3"; break;
				case DataType::color3_t: base = "color3"; break;
				case DataType::color4_t: base = "color4"; break;
				case DataType::asset_t: base = "Asset"; break;
				case DataType::node_t: base = field.node_type.empty() ? "Node" : field.node_type; break;
				case DataType::object_t: base = emitStruct(field); break;
				default: base = "any"; break;
			}
		}

		return field.is_array ? base + "[]" : base;
	}

	static auto enumUnion(const std::vector<std::string>& options) -> std::string {
		std::string result;
		for (size_t i = 0; i < options.size(); ++i) {
			if (i > 0) {
				result += '|';
			}
			result += '\"' + options[i] + '\"';
		}
		return result;
	}

	auto emitStruct(const SchemaField& field) -> std::string {
		const std::string local_name = pascalCase(field.struct_type.empty() ? field.name : field.struct_type);
		const std::string class_name = m_root_name + '.' + local_name;
		if (m_emitted_classes.insert(class_name).second) {
			emitClass(class_name, field.children, "");
		}
		return class_name;
	}

	void emitClass(const std::string& class_name, const std::vector<SchemaField>& fields, std::string_view base) {
		// Resolve field types first
		std::vector<std::pair<const SchemaField*, std::string>> resolved;
		resolved.reserve(fields.size());
		for (const auto& f : fields) {
			resolved.emplace_back(&f, luaTypeOf(f));
		}

		m_out << "---@class " << class_name;
		if (!base.empty()) {
			m_out << " : " << base;
		}
		m_out << "\n";
		for (const auto& [f, type_str] : resolved) {
			m_out << "---@field " << f->name << (f->variants.empty() ? "" : "?") << " " << type_str;
			if (!f->description.empty()) {
				m_out << " # " << f->description;
			}
			m_out << "\n";
		}
		m_out << "\n";
	}
};

}

auto namedSchemaEntries() -> std::vector<NamedSchemaEntry> {
	auto& mgr = AssetManager::get();
	std::vector<NamedSchemaEntry> entries;
	for (const auto uid : mgr.listByType("schema")) {
		auto handle = load<Schema>(uid);
		if (!handle.hasValue() || !handle->isValid()) {
			TOAST_WARN("DataSchemaCodegen", "Skipping schema {} ({}): failed to load or parse", uid, AssetManager::getURI(uid));
			continue;
		}
		entries.push_back({uid, rootNameFromPath(AssetManager::getURI(uid)), std::move(handle)});
	}

	std::unordered_map<std::string, int> name_counts;
	for (const auto& e : entries) {
		++name_counts[e.name];
	}
	for (auto& e : entries) {
		if (name_counts[e.name] > 1) {
			e.name += "_" + toast::UID::toString(e.uid.data());
		}
	}

	return entries;
}

auto generateDataSchemaLuaStubs(const std::filesystem::path& out_path) -> bool {
	const auto entries = namedSchemaEntries();

	std::ostringstream out;
	out << "-- ============================================================\n"
	    << "-- AUTO-GENERATED FILE - DO NOT MODIFY DIRECTLY\n"
	    << "-- Changes will not persist\n"
	    << "-- ============================================================\n\n";

	for (const auto& e : entries) {
		Emitter emitter("Schemas." + e.name);
		emitter.emitRoot(e.handle->fields());
		out << emitter.text();
	}

	if (!entries.empty()) {
		out << "---@class SchemasTable\n";
		for (const auto& e : entries) {
			out << "---@field " << e.name << " Schemas." << e.name << "\n";
		}
		out << "---@type SchemasTable\n";
		out << "Schemas = nil\n";
	}

	std::error_code ec;
	std::filesystem::create_directories(out_path.parent_path(), ec);

	auto tmp_path = out_path;
	tmp_path += ".tmp";
	{
		std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
		if (!file) {
			TOAST_ERROR("DataSchemaCodegen", "Failed to open '{}' for writing", tmp_path.string());
			return false;
		}
		const auto content = out.str();
		file.write(content.data(), static_cast<std::streamsize>(content.size()));
	}

	std::filesystem::rename(tmp_path, out_path, ec);
	if (ec) {
		TOAST_ERROR("DataSchemaCodegen", "Failed to move '{}' to '{}': {}", tmp_path.string(), out_path.string(), ec.message());
		return false;
	}

	TOAST_INFO("DataSchemaCodegen", "Generated {} schema stub(s) -> '{}'", entries.size(), out_path.string());
	return true;
}

}
