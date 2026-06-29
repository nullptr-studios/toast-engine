#include "schema.hpp"

#include <toast/log.hpp>

namespace assets {

Schema::Schema(std::string_view json_content) {
	try {
		auto j = json_t::parse(json_content);

		if (j.contains("title") && j["title"].is_string()) {
			m_title = j["title"].get<std::string>();
		}

		// parse named definitions
		if (j.contains("definitions") && j["definitions"].is_object()) {
			for (const auto& [name, def_obj] : j["definitions"].items()) {
				if (!def_obj.is_object()) {
					continue;
				}
				std::vector<SchemaField> def_fields;
				if (def_obj.contains("properties") && def_obj["properties"].is_object()) {
					parseFields(def_obj["properties"], def_fields);
				}
				m_definitions[name] = std::move(def_fields);
			}
		}

		// parse top-level properties
		if (j.contains("properties") && j["properties"].is_object()) {
			parseFields(j["properties"], m_fields);
		}

		// resolve struct_type references
		resolveStructRefs(m_fields);
		for (auto& [name, fields] : m_definitions) {
			resolveStructRefs(fields);
		}

	} catch (const json_t::exception& e) { TOAST_ERROR("Schema", "Failed to parse schema JSON: {}", e.what()); }
}

auto Schema::hasDefinition(std::string_view name) const noexcept -> bool {
	return m_definitions.contains(std::string(name));
}

auto Schema::getDefinition(std::string_view name) const -> const std::vector<SchemaField>* {
	auto it = m_definitions.find(std::string(name));
	return it != m_definitions.end() ? &it->second : nullptr;
}

auto Schema::toastTypeOf(const std::string& x_type) -> DataType {
	if (x_type == "bool") {
		return DataType::bool_t;
	}
	if (x_type == "int") {
		return DataType::int_t;
	}
	if (x_type == "float") {
		return DataType::float_t;
	}
	if (x_type == "string") {
		return DataType::string_t;
	}
	if (x_type == "node") {
		return DataType::node_t;
	}
	if (x_type == "asset") {
		return DataType::asset_t;
	}
	if (x_type == "vec2") {
		return DataType::vec2_t;
	}
	if (x_type == "vec3") {
		return DataType::vec3_t;
	}
	if (x_type == "color3") {
		return DataType::color3_t;
	}
	if (x_type == "color4") {
		return DataType::color4_t;
	}
	if (x_type == "object") {
		return DataType::object_t;
	}
	// Anything else is a named struct type
	return DataType::object_t;
}

auto Schema::parseDefault(DataType t, bool is_array, const json_t& j_value) -> DataValue {
	if (is_array) {
		auto arr = DataValue::makeArray();
		if (j_value.is_array()) {
			for (const auto& elem : j_value) {
				arr.push(parseDefault(t, false, elem));
			}
		}
		return arr;
	}
	switch (t) {
		case DataType::bool_t: return DataValue(j_value.is_boolean() ? j_value.get<bool>() : false);
		case DataType::int_t: return DataValue(j_value.is_number_integer() ? j_value.get<int64_t>() : int64_t {0});
		case DataType::float_t: return DataValue(j_value.is_number() ? j_value.get<double>() : 0.0);
		case DataType::string_t: return DataValue(j_value.is_string() ? j_value.get<std::string>() : std::string {});
		case DataType::node_t:
		case DataType::asset_t: {
			if (j_value.is_string()) {
				auto s = j_value.get<std::string>();
				if (s.size() == 11) {
					return {toast::UID(toast::UID::fromString(s)), t};
				}
			}
			return DataValue(toast::UID(uint64_t {0}), t);
		}
		case DataType::vec2_t: {
			if (j_value.is_array() && j_value.size() >= 2) {
				return DataValue(glm::vec2 {static_cast<float>(j_value[0].get<double>()), static_cast<float>(j_value[1].get<double>())});
			}
			return DataValue(glm::vec2 {0.f});
		}
		case DataType::vec3_t: {
			if (j_value.is_array() && j_value.size() >= 3) {
				return {
				  glm::vec3 {
				             static_cast<float>(j_value[0].get<double>()),
				             static_cast<float>(j_value[1].get<double>()),
				             static_cast<float>(j_value[2].get<double>())
				  },
				  DataType::vec3_t
				};
			}
			return {glm::vec3 {0.f}, DataType::vec3_t};
		}
		case DataType::color3_t: {
			if (j_value.is_array() && j_value.size() >= 3) {
				return {
				  glm::vec3 {
				             static_cast<float>(j_value[0].get<double>()),
				             static_cast<float>(j_value[1].get<double>()),
				             static_cast<float>(j_value[2].get<double>())
				  },
				  DataType::color3_t
				};
			}
			return {glm::vec3 {0.f}, DataType::color3_t};
		}
		case DataType::color4_t: {
			if (j_value.is_array() && j_value.size() >= 4) {
				return DataValue(
				    glm::vec4 {
				      static_cast<float>(j_value[0].get<double>()),
				      static_cast<float>(j_value[1].get<double>()),
				      static_cast<float>(j_value[2].get<double>()),
				      static_cast<float>(j_value[3].get<double>())
				    }
				);
			}
			return DataValue(glm::vec4 {0.f, 0.f, 0.f, 1.f});
		}
		default: return DataValue {};
	}
}

auto Schema::parseOneField(const std::string& name, const json_t& properties) -> SchemaField {
	SchemaField field;
	field.name = name;

	// determine type
	std::string x_type;
	bool x_type_found = false;

	if (properties.contains("x-toast-type") && properties["x-toast-type"].is_string()) {
		x_type = properties["x-toast-type"].get<std::string>();
		x_type_found = true;
	}

	if (x_type_found) {
		// Strip "[]" suffix
		if (x_type.size() > 2 && x_type.ends_with("[]")) {
			field.is_array = true;
			x_type = x_type.substr(0, x_type.size() - 2);
		}

		field.type = toastTypeOf(x_type);

		// If the x-toast-type is a named struct
		if (field.type == DataType::object_t && x_type != "object") {
			field.struct_type = x_type;
		}

	} else {
		// Fallback: infer from schema type
		if (properties.contains("type") && properties["type"].is_string()) {
			const auto j_type = properties["type"].get<std::string>();
			if (j_type == "boolean") {
				field.type = DataType::bool_t;
			} else if (j_type == "integer") {
				field.type = DataType::int_t;
			} else if (j_type == "number") {
				field.type = DataType::float_t;
			} else if (j_type == "string") {
				field.type = DataType::string_t;
			} else if (j_type == "array") {
				field.is_array = true;
			} else if (j_type == "object") {
				field.type = DataType::object_t;
			}
		}
	}

	// Description
	if (properties.contains("description") && properties["description"].is_string()) {
		field.description = properties["description"].get<std::string>();
	}

	// Numeric constraints
	if (properties.contains("minimum") && properties["minimum"].is_number()) {
		field.min = properties["minimum"].get<double>();
	}
	if (properties.contains("maximum") && properties["maximum"].is_number()) {
		field.max = properties["maximum"].get<double>();
	}

	// Default value
	if (properties.contains("default") && !properties["default"].is_null()) {
		field.default_value = parseDefault(field.type, field.is_array, properties["default"]);
	}

	// Inline object children
	if (field.type == DataType::object_t && field.struct_type.empty() && properties.contains("properties") &&
	    properties["properties"].is_object()) {
		parseFields(properties["properties"], field.children);
	}

	// Array element type
	// When is_array==true but no x-toast-type was set, try to infer from "items"
	if (field.is_array && field.type == DataType::null && properties.contains("items") && properties["items"].is_object()) {
		const auto& items = properties["items"];
		if (items.contains("x-toast-type") && items["x-toast-type"].is_string()) {
			auto et = items["x-toast-type"].get<std::string>();
			field.type = toastTypeOf(et);
			if (field.type == DataType::object_t && et != "object") {
				field.struct_type = et;
			}
		} else if (items.contains("type") && items["type"].is_string()) {
			const auto jt = items["type"].get<std::string>();
			if (jt == "boolean") {
				field.type = DataType::bool_t;
			} else if (jt == "integer") {
				field.type = DataType::int_t;
			} else if (jt == "number") {
				field.type = DataType::float_t;
			} else if (jt == "string") {
				field.type = DataType::string_t;
			} else if (jt == "object") {
				field.type = DataType::object_t;
			}
		}
	}

	return field;
}

void Schema::parseFields(const json_t& properties, std::vector<SchemaField>& out) {
	for (const auto& [name, prop] : properties.items()) {
		if (!prop.is_object()) {
			continue;
		}
		out.push_back(parseOneField(name, prop));
	}
}

void Schema::resolveStructRefs(std::vector<SchemaField>& fields) {
	for (auto& field : fields) {
		if (field.type == DataType::object_t && !field.struct_type.empty() && field.children.empty()) {
			auto it = m_definitions.find(field.struct_type);
			if (it != m_definitions.end()) {
				field.children = it->second;
				resolveStructRefs(field.children);    // resolve nested refs too
			} else {
				TOAST_WARN("Schema", "Struct type '{}' not found in definitions for field '{}'", field.struct_type, field.name);
			}
		} else if (!field.children.empty()) {
			resolveStructRefs(field.children);
		}
	}
}

}
