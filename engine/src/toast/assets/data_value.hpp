/**
 * @file data_value.hpp
 * @author Xein
 * @date 28 Jun 2026
 *
 * @brief Recursive typed value for schema-driven TOML data assets
 */

#pragma once

#include <any>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <toast/export.hpp>
#include <toast/log.hpp>
#include <toast/uid.hpp>
#include <toml++/toml.hpp>
#include <vector>

using json_t = nlohmann::json;

namespace assets {

enum class DataType : uint8_t {
	null,        // empty
	bool_t,      // bool
	int_t,       // int64_t
	float_t,     // double
	string_t,    // std::string
	node_t,      // toast::UID
	asset_t,     // toast::UID
	vec2_t,      // glm::vec2
	vec3_t,      // glm::vec3
	color3_t,    // glm::vec3
	color4_t,    // glm::vec4
	array_t,     // std::vector<DataValue>
	object_t,    // DataValue::ObjectMap
};

struct SchemaField;

/**
 * @brief Typed value for TOML files
 */
class TOAST_API DataValue {
public:
	using ObjectMap = std::vector<std::pair<std::string, DataValue>>;
	using Array = std::vector<DataValue>;

	DataValue() = default;

	explicit DataValue(bool v) : m_type(DataType::bool_t), m_value(v) { }

	explicit DataValue(int64_t v) : m_type(DataType::int_t), m_value(v) { }

	explicit DataValue(double v) : m_type(DataType::float_t), m_value(v) { }

	explicit DataValue(std::string v) : m_type(DataType::string_t), m_value(std::move(v)) { }

	DataValue(toast::UID v, DataType kind) : m_type(kind), m_value(v) {
		TOAST_ASSERT(
		    kind == DataType::node_t || kind == DataType::asset_t, "DataValue", "UID constructor requires Node or Asset DataType"
		);
	}

	explicit DataValue(glm::vec2 v) : m_type(DataType::vec2_t), m_value(v) { }

	DataValue(glm::vec3 v, DataType kind) : m_type(kind), m_value(v) {
		TOAST_ASSERT(
		    kind == DataType::vec3_t || kind == DataType::color3_t, "DataValue", "vec3 constructor requires Vec3 or Color3 DataType"
		);
	}

	explicit DataValue(glm::vec4 v) : m_type(DataType::color4_t), m_value(v) { }

	static auto makeArray() -> DataValue;
	static auto makeObject() -> DataValue;

	[[nodiscard]]
	auto type() const noexcept -> DataType {
		return m_type;
	}

	[[nodiscard]]
	auto isNull() const noexcept -> bool {
		return m_type == DataType::null;
	}

	[[nodiscard]]
	auto isArray() const noexcept -> bool {
		return m_type == DataType::array_t;
	}

	[[nodiscard]]
	auto isObject() const noexcept -> bool {
		return m_type == DataType::object_t;
	}

	template<class T>
	auto as() const -> T {
		TOAST_ASSERT(m_value.has_value(), "DataValue", "as<T>() called on a Null DataValue");
		if (const T* ptr = std::any_cast<T>(&m_value)) {
			return *ptr;
		}
		TOAST_ASSERT(false, "DataValue", "Type mismatch in DataValue::as<T>()");
		return T {};
	}

	template<class T>
	auto value() const -> std::optional<T> {
		if (!m_value.has_value()) {
			return std::nullopt;
		}
		if (const T* ptr = std::any_cast<T>(&m_value)) {
			return *ptr;
		}
		TOAST_WARN("DataValue", "Type mismatch in DataValue::value<T>()");
		return std::nullopt;
	}

	explicit operator bool() const;
	explicit operator int64_t() const;
	explicit operator int() const;
	explicit operator float() const;
	explicit operator double() const;
	explicit operator std::string() const;
	explicit operator toast::UID() const;
	explicit operator glm::vec2() const;
	explicit operator glm::vec3() const;
	explicit operator glm::vec4() const;

	auto operator[](std::string_view key) -> DataValue&;
	auto operator[](std::string_view key) const -> const DataValue&;

	[[nodiscard]]
	auto contains(std::string_view key) const noexcept -> bool;

	auto operator[](size_t index) -> DataValue&;
	auto operator[](size_t index) const -> const DataValue&;

	[[nodiscard]]
	auto size() const noexcept -> size_t;

	[[nodiscard]]
	auto empty() const noexcept -> bool {
		return size() == 0;
	}

	void push(DataValue v);                    ///< Array: appends an element
	void set(std::string key, DataValue v);    ///< Object: inserts or replaces by key
	void remove(std::string_view key);         ///< Object: removes a key (no-op if absent)

	[[nodiscard]]
	auto items() -> ObjectMap&;
	[[nodiscard]]
	auto items() const -> const ObjectMap&;

	auto operator=(bool v) -> DataValue&;
	auto operator=(int64_t v) -> DataValue&;
	auto operator=(int v) -> DataValue&;
	auto operator=(double v) -> DataValue&;
	auto operator=(float v) -> DataValue&;
	auto operator=(std::string v) -> DataValue&;

	/**
	 * @brief Parse a DataValue from a TOML node, using the schema field for type context
	 */
	static auto fromToml(const toml::node& n, const SchemaField* field) -> DataValue;

	/**
	 * @brief Build an Object DataValue from a TOML table, guided by a field list
	 */
	static auto fromObject(const toml::table& t, const std::vector<SchemaField>& fields) -> DataValue;

	/** Inserts this value into a TOML table under @c key */
	void appendTo(toml::table& tbl, const std::string& key) const;

	/** Appends this value to a TOML array */
	void appendTo(toml::array& arr) const;

	/** Converts an Object DataValue to a toml::table */
	[[nodiscard]]
	auto asTomlTable() const -> toml::table;

	// used as the intermediate for BSON
	[[nodiscard]]
	auto toJson() const -> json_t;

	/** Reconstruct a DataValue from nlohmann::json */
	static auto fromJson(const json_t& j, DataType hint = DataType::null) -> DataValue;

private:
	DataType m_type {DataType::null};
	std::any m_value;
};

}
