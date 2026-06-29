/**
 * @file schema.hpp
 * @author Xein
 * @date 28 Jun 2026
 *
 * @brief Used as "reflection data" for @c Data types
 */

#pragma once

#include "core_types.hpp"
#include "data_value.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace assets {

/**
 * @brief Descriptor for one field in a schema
 */
struct TOAST_API SchemaField {
	std::string name;
	DataType type {DataType::null};

	bool is_array {false};
	std::string struct_type;

	std::optional<DataValue> default_value;
	std::string description;

	std::optional<double> min;
	std::optional<double> max;

	std::vector<SchemaField> children;
};

/**
 * @brief Asset wrapping a parsed `.schema.json`
 */
class TOAST_API Schema : public Asset {
public:
	explicit Schema(std::string_view json_content);

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "schema";
	}

	[[nodiscard]]
	auto title() const -> std::string_view {
		return m_title;
	}

	[[nodiscard]]
	auto fields() const -> const std::vector<SchemaField>& {
		return m_fields;
	}

	[[nodiscard]]
	auto hasDefinition(std::string_view name) const noexcept -> bool;

	[[nodiscard]]
	auto getDefinition(std::string_view name) const -> const std::vector<SchemaField>*;

	[[nodiscard]]
	auto definitions() const -> const std::map<std::string, std::vector<SchemaField>>& {
		return m_definitions;
	}

private:
	std::string m_title;
	std::vector<SchemaField> m_fields;
	std::map<std::string, std::vector<SchemaField>> m_definitions;

	/** Parse one property entry into a SchemaField */
	static auto parseOneField(const std::string& name, const json_t& properties) -> SchemaField;

	/** Parse "properties" + "required" into a flat field list */
	static void parseFields(const json_t& properties, std::vector<SchemaField>& out);

	/** Parse a default JSON value into a DataValue given its DataType */
	static auto parseDefault(DataType t, bool is_array, const json_t& j_value) -> DataValue;

	/** Map an x-toast-type string to a DataType; returns Null for unknowns */
	static auto toastTypeOf(const std::string& x_type) -> DataType;

	/**
	 * @brief Recursively resolve struct_type refs: copy definition fields into children
	 *
	 * Called after m_definitions is fully populated so all refs can be satisfied
	 */
	void resolveStructRefs(std::vector<SchemaField>& fields);
};

}
