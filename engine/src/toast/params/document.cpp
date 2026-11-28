#include "document.hpp"

#include <sstream>
#include <toml++/toml.hpp>

namespace toast::params {
namespace {

auto toJson(const toml::node& node) -> Document {
	switch (node.type()) {
		case toml::node_type::table: {
			auto object = Document::object();
			for (const auto& [key, child] : *node.as_table()) {
				object[std::string {key.str()}] = toJson(child);
			}
			return object;
		}
		case toml::node_type::array: {
			auto array = Document::array();
			for (const auto& child : *node.as_array()) {
				array.push_back(toJson(child));
			}
			return array;
		}
		case toml::node_type::string: return node.as_string()->get();
		case toml::node_type::integer: return node.as_integer()->get();
		case toml::node_type::floating_point: return node.as_floating_point()->get();
		case toml::node_type::boolean: return node.as_boolean()->get();
		default: {
			std::ostringstream text;
			text << toml::toml_formatter {node};
			return text.str();
		}
	}
}

auto toTable(const Document& object) -> toml::table;
auto toArray(const Document& array) -> toml::array;

void put(toml::table& table, const std::string& key, const Document& value) {
	if (value.is_object()) {
		table.insert_or_assign(key, toTable(value));
	} else if (value.is_array()) {
		table.insert_or_assign(key, toArray(value));
	} else if (value.is_boolean()) {
		table.insert_or_assign(key, value.get<bool>());
	} else if (value.is_number_integer()) {
		table.insert_or_assign(key, value.get<int64_t>());
	} else if (value.is_number_float()) {
		table.insert_or_assign(key, value.get<double>());
	} else if (value.is_string()) {
		table.insert_or_assign(key, value.get<std::string>());
	}
}

void push(toml::array& array, const Document& value) {
	if (value.is_object()) {
		array.push_back(toTable(value));
	} else if (value.is_array()) {
		array.push_back(toArray(value));
	} else if (value.is_boolean()) {
		array.push_back(value.get<bool>());
	} else if (value.is_number_integer()) {
		array.push_back(value.get<int64_t>());
	} else if (value.is_number_float()) {
		array.push_back(value.get<double>());
	} else if (value.is_string()) {
		array.push_back(value.get<std::string>());
	}
}

auto toTable(const Document& object) -> toml::table {
	toml::table table;
	for (const auto& [key, value] : object.items()) {
		put(table, key, value);
	}
	return table;
}

auto toArray(const Document& array) -> toml::array {
	toml::array out;
	for (const auto& value : array) {
		push(out, value);
	}
	return out;
}

}

auto tomlToDocument(std::string_view text) -> std::expected<Document, std::string> {
	try {
		const toml::table table = toml::parse(text);
		return toJson(table);
	} catch (const toml::parse_error& error) {
		return std::unexpected(std::string {error.description()});
	} catch (const std::exception& error) { return std::unexpected(std::string {error.what()}); }
}

auto documentToToml(const Document& document) -> std::string {
	std::ostringstream out;
	out << toml::toml_formatter {toTable(document.is_object() ? document : Document::object())};
	return out.str();
}

}
