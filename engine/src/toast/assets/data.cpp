#include "data.hpp"

#include <sstream>
#include <toast/log.hpp>

namespace assets {

Data::Data(const toml::table& table, AssetHandle<Schema> schema) : m_schema(std::move(schema)) {
	const Schema* schema_ptr = m_schema.hasValue() ? &m_schema.get() : nullptr;
	m_root = buildRoot(table, schema_ptr);
}

Data::Data(const toml::table& table, AssetHandle<Schema> schema, KeepAllKeysTag) : m_schema(std::move(schema)) {
	m_root = buildRoot(table, nullptr);
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
