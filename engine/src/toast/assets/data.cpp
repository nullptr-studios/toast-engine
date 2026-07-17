#include "data.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <limits>
#include <sstream>
#include <toast/log.hpp>

namespace assets {

namespace {

void applyConstraints(DataValue& value, const std::vector<SchemaField>& fields);

void applyFieldConstraint(DataValue& v, const SchemaField& field) {
	if (!field.children.empty()) {
		applyConstraints(v, field.children);
		return;
	}
	if (!field.min.has_value() && !field.max.has_value()) {
		return;
	}
	const auto lo = static_cast<float>(field.min.value_or(std::numeric_limits<double>::lowest()));
	const auto hi = static_cast<float>(field.max.value_or(std::numeric_limits<double>::max()));
	switch (v.type()) {
		case DataType::int_t:
			v = static_cast<int64_t>(
			    std::clamp(static_cast<double>(v.as<int64_t>()), static_cast<double>(lo), static_cast<double>(hi))
			);
			break;
		case DataType::float_t: v = std::clamp(v.as<double>(), static_cast<double>(lo), static_cast<double>(hi)); break;
		case DataType::vec2_t: v = DataValue(glm::clamp(v.as<glm::vec2>(), lo, hi)); break;
		case DataType::vec3_t: v = DataValue(glm::clamp(v.as<glm::vec3>(), lo, hi), DataType::vec3_t); break;
		default: break;
	}
}

/// Clamps values that exist in the data to their schema min/max
void applyConstraints(DataValue& value, const std::vector<SchemaField>& fields) {
	if (value.type() != DataType::object_t) {
		return;
	}
	for (const auto& field : fields) {
		if (!value.contains(field.name)) {
			continue;
		}
		auto& v = value[field.name];
		if (v.isArray()) {
			for (size_t i = 0; i < v.size(); ++i) {
				applyFieldConstraint(v[i], field);
			}
		} else {
			applyFieldConstraint(v, field);
		}
	}
}

}

Data::Data(const toml::table& table, AssetHandle<Schema> schema) : m_schema(std::move(schema)) {
	const Schema* schema_ptr = m_schema.hasValue() ? &m_schema.get() : nullptr;
	m_root = buildRoot(table, schema_ptr);
	if (schema_ptr != nullptr) {
		applyConstraints(m_root, schema_ptr->fields());
	}
}

Data::Data(const toml::table& table, AssetHandle<Schema> schema, KeepAllKeysTag) : m_schema(std::move(schema)) {
	m_keep_all_keys = true;
	m_root = buildRoot(table, nullptr);
	// KeepAllKeys skips the schema when building
	if (m_schema.hasValue()) {
		applyConstraints(m_root, m_schema.get().fields());
	}
}

void Data::reload(const toml::table& table) {
	const Schema* schema_ptr = (m_schema.hasValue() && !m_keep_all_keys) ? &m_schema.get() : nullptr;
	m_root = buildRoot(table, schema_ptr);
	if (m_schema.hasValue()) {
		applyConstraints(m_root, m_schema.get().fields());
	}
}

auto Data::buildRoot(const toml::table& table, const Schema* schema) -> DataValue {
	auto root = DataValue::makeObject();

	if (schema == nullptr) {
		// include all keys except the "schema" metadata key
		for (const auto& [k, v] : table) {
			std::string key(k.str());
			if (key == "schema") {
				continue;
			}
			root.set(key, DataValue::fromToml(v, nullptr));
		}
		return root;
	}

	// emit exactly the schema fields in order
	for (const auto& field : schema->fields()) {
		auto it = table.find(field.name);
		if (it != table.end()) {
			root.set(field.name, DataValue::fromToml(it->second, &field));
		} else if (field.default_value.has_value()) {
			root.set(field.name, *field.default_value);
		} else {
			root.set(field.name, DataValue {});    // Null placeholder
		}
	}
	return root;
}

auto Data::serialize(SaveMode mode) const -> std::vector<uint8_t> {
	if (mode == SaveMode::game) {
		// BSON, write only
		auto j = m_root.toJson();
		if (m_schema.hasValue()) {
			j["schema"] = m_schema.uid().get();
		}
		auto bson = json_t::to_bson(j);
		return bson;
	}

	toml::table tbl;

	if (m_schema.hasValue()) {
		tbl.insert_or_assign("schema", m_schema.uid().get());
	}

	if (m_root.type() == DataType::object_t) {
		for (const auto& [k, v] : m_root.items()) {
			v.appendTo(tbl, k);
		}
	}

	std::ostringstream ss;
	ss << tbl;
	auto str = ss.str();
	return {str.begin(), str.end()};
}

}
