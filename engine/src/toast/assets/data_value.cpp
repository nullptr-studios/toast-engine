#include "data_value.hpp"

#include "schema.hpp"

#include <toast/log.hpp>

namespace assets {

auto DataValue::makeArray() -> DataValue {
	DataValue v;
	v.m_type = DataType::array_t;
	v.m_value = Array {};
	return v;
}

auto DataValue::makeObject() -> DataValue {
	DataValue v;
	v.m_type = DataType::object_t;
	v.m_value = ObjectMap {};
	return v;
}

DataValue::operator bool() const {
	TOAST_ASSERT(m_type == DataType::bool_t, "AssetManager", "operator bool() on non-Bool (type={})", static_cast<int>(m_type));
	return std::any_cast<bool>(m_value);
}

DataValue::operator int64_t() const {
	TOAST_ASSERT(m_type == DataType::int_t, "AssetManager", "operator int64_t() on non-Int (type={})", static_cast<int>(m_type));
	return std::any_cast<int64_t>(m_value);
}

DataValue::operator int() const {
	TOAST_ASSERT(m_type == DataType::int_t, "AssetManager", "operator int() on non-Int (type={})", static_cast<int>(m_type));
	return static_cast<int>(std::any_cast<int64_t>(m_value));
}

DataValue::operator float() const {
	TOAST_ASSERT(m_type == DataType::float_t, "AssetManager", "operator float() on non-Float (type={})", static_cast<int>(m_type));
	return static_cast<float>(std::any_cast<double>(m_value));
}

DataValue::operator double() const {
	TOAST_ASSERT(m_type == DataType::float_t, "AssetManager", "operator double() on non-Float (type={})", static_cast<int>(m_type));
	return std::any_cast<double>(m_value);
}

DataValue::operator std::string() const {
	TOAST_ASSERT(
	    m_type == DataType::string_t, "AssetManager", "operator string() on non-String (type={})", static_cast<int>(m_type)
	);
	return std::any_cast<std::string>(m_value);
}

DataValue::operator toast::UID() const {
	TOAST_ASSERT(
	    m_type == DataType::node_t || m_type == DataType::asset_t,
	    "AssetManager",
	    "operator UID() on non-Node/Asset (type={})",
	    static_cast<int>(m_type)
	);
	return std::any_cast<toast::UID>(m_value);
}

DataValue::operator glm::vec2() const {
	TOAST_ASSERT(m_type == DataType::vec2_t, "AssetManager", "operator vec2() on non-Vec2 (type={})", static_cast<int>(m_type));
	return std::any_cast<glm::vec2>(m_value);
}

DataValue::operator glm::vec3() const {
	TOAST_ASSERT(
	    m_type == DataType::vec3_t || m_type == DataType::color3_t,
	    "AssetManager",
	    "operator vec3() on non-Vec3/Color3 (type={})",
	    static_cast<int>(m_type)
	);
	return std::any_cast<glm::vec3>(m_value);
}

DataValue::operator glm::vec4() const {
	TOAST_ASSERT(m_type == DataType::color4_t, "AssetManager", "operator vec4() on non-Color4 (type={})", static_cast<int>(m_type));
	return std::any_cast<glm::vec4>(m_value);
}

auto DataValue::operator[](std::string_view key) -> DataValue& {
	TOAST_ASSERT(m_type == DataType::object_t, "AssetManager", "operator[](key) on non-Object (type={})", static_cast<int>(m_type));
	auto& map = std::any_cast<ObjectMap&>(m_value);
	for (auto& [k, v] : map) {
		if (k == key) {
			return v;
		}
	}
	map.emplace_back(std::string(key), DataValue {});
	return map.back().second;
}

auto DataValue::operator[](std::string_view key) const -> const DataValue& {
	TOAST_ASSERT(m_type == DataType::object_t, "AssetManager", "operator[](key) on non-Object (type={})", static_cast<int>(m_type));
	const auto& map = std::any_cast<const ObjectMap&>(m_value);
	for (const auto& [k, v] : map) {
		if (k == key) {
			return v;
		}
	}
	TOAST_WARN("AssetManager", "Key '{}' not found in Object DataValue", key);
	static const DataValue s_null {};
	return s_null;
}

auto DataValue::contains(std::string_view key) const noexcept -> bool {
	if (m_type != DataType::object_t) {
		return false;
	}
	try {
		const auto& map = std::any_cast<const ObjectMap&>(m_value);
		for (const auto& [k, v] : map) {
			if (k == key) {
				return true;
			}
		}
	} catch (const std::exception& e) {
		TOAST_ERROR("AssetManager", "Error while trying DataValue::contains({}): {}", key, e.what());
	}
	return false;
}

auto DataValue::operator[](size_t index) -> DataValue& {
	TOAST_ASSERT(m_type == DataType::array_t, "AssetManager", "operator[](index) on non-Array (type={})", static_cast<int>(m_type));
	auto& arr = std::any_cast<Array&>(m_value);
	TOAST_ASSERT(index < arr.size(), "AssetManager", "Array index {} out of bounds (size={})", index, arr.size());
	return arr[index];
}

auto DataValue::operator[](size_t index) const -> const DataValue& {
	TOAST_ASSERT(m_type == DataType::array_t, "AssetManager", "operator[](index) on non-Array (type={})", static_cast<int>(m_type));
	const auto& arr = std::any_cast<const Array&>(m_value);
	TOAST_ASSERT(index < arr.size(), "AssetManager", "Array index {} out of bounds (size={})", index, arr.size());
	return arr[index];
}

auto DataValue::size() const noexcept -> size_t {
	try {
		if (m_type == DataType::array_t) {
			return std::any_cast<const Array&>(m_value).size();
		}
		if (m_type == DataType::object_t) {
			return std::any_cast<const ObjectMap&>(m_value).size();
		}
	} catch (const std::exception& e) { TOAST_ERROR("AssetManager", "Error while trying DataValue::size(): {}", e.what()); }
	return 0;
}

void DataValue::push(DataValue v) {
	TOAST_ASSERT(m_type == DataType::array_t, "AssetManager", "push() on non-Array");
	std::any_cast<Array&>(m_value).push_back(std::move(v));
}

void DataValue::set(std::string key, DataValue v) {
	TOAST_ASSERT(m_type == DataType::object_t, "AssetManager", "set() on non-Object");
	auto& map = std::any_cast<ObjectMap&>(m_value);
	for (auto& [k, val] : map) {
		if (k == key) {
			val = std::move(v);
			return;
		}
	}
	map.emplace_back(std::move(key), std::move(v));
}

void DataValue::remove(std::string_view key) {
	if (m_type != DataType::object_t) {
		return;
	}
	auto& map = std::any_cast<ObjectMap&>(m_value);
	std::erase_if(map, [key](const auto& p) { return p.first == key; });
}

auto DataValue::items() -> ObjectMap& {
	TOAST_ASSERT(m_type == DataType::object_t, "AssetManager", "items() on non-Object");
	return std::any_cast<ObjectMap&>(m_value);
}

auto DataValue::items() const -> const ObjectMap& {
	TOAST_ASSERT(m_type == DataType::object_t, "AssetManager", "items() on non-Object");
	return std::any_cast<const ObjectMap&>(m_value);
}

auto DataValue::operator=(bool v) -> DataValue& {
	m_type = DataType::bool_t;
	m_value = v;
	return *this;
}

auto DataValue::operator=(int64_t v) -> DataValue& {
	m_type = DataType::int_t;
	m_value = v;
	return *this;
}

auto DataValue::operator=(int v) -> DataValue& {
	m_type = DataType::int_t;
	m_value = int64_t(v);
	return *this;
}

auto DataValue::operator=(double v) -> DataValue& {
	m_type = DataType::float_t;
	m_value = v;
	return *this;
}

auto DataValue::operator=(float v) -> DataValue& {
	m_type = DataType::float_t;
	m_value = double(v);
	return *this;
}

auto DataValue::operator=(std::string v) -> DataValue& {
	m_type = DataType::string_t;
	m_value = std::move(v);
	return *this;
}

static auto extractNumbers(const toml::array& arr, size_t count) -> std::vector<double> {
	std::vector<double> nums;
	nums.reserve(count);
	for (size_t i = 0; i < std::min(arr.size(), count); ++i) {
		if (auto v = arr[i].value<double>()) {
			nums.push_back(*v);
		} else if (auto vi = arr[i].value<int64_t>()) {
			nums.push_back(static_cast<double>(*vi));
		} else {
			nums.push_back(0.0);
		}
	}
	while (nums.size() < count) {
		nums.push_back(0.0);
	}
	return nums;
}

auto DataValue::fromToml(const toml::node& n, const SchemaField* field) -> DataValue {
	// Schema-less mode
	if (field == nullptr) {
		switch (n.type()) {
			case toml::node_type::boolean: return DataValue(*n.value<bool>());
			case toml::node_type::integer: return DataValue(static_cast<int64_t>(*n.value<int64_t>()));
			case toml::node_type::floating_point: return DataValue(*n.value<double>());
			case toml::node_type::string: return DataValue(std::string(*n.value<std::string_view>()));
			case toml::node_type::array: {
				auto result = DataValue::makeArray();
				for (const auto& elem : *n.as_array()) {
					result.push(fromToml(elem, nullptr));
				}
				return result;
			}
			case toml::node_type::table: {
				return fromObject(*n.as_table(), {});
			}
			default: return DataValue {};
		}
	}

	// Array field
	if (field->is_array) {
		const auto* arr = n.as_array();
		if (!arr) {
			TOAST_WARN("AssetManager", "Expected array for field '{}', got scalar; returning empty array", field->name);
			return DataValue::makeArray();
		}
		SchemaField elem_field = *field;
		elem_field.is_array = false;
		auto result = DataValue::makeArray();
		for (const auto& elem : *arr) {
			result.push(fromToml(elem, &elem_field));
		}
		return result;
	}

	// Object/leaf
	switch (field->type) {
		case DataType::bool_t: {
			if (auto v = n.value<bool>()) {
				return DataValue(*v);
			}
			// Attempt integer
			if (auto v = n.value<int64_t>()) {
				return DataValue(*v != 0);
			}
			TOAST_WARN("AssetManager", "Expected bool for field '{}'; defaulting to false", field->name);
			return DataValue(false);
		}

		case DataType::int_t: {
			if (auto v = n.value<int64_t>()) {
				return DataValue(*v);
			}
			TOAST_WARN("AssetManager", "Expected int for field '{}'; defaulting to 0", field->name);
			return DataValue(int64_t {0});
		}

		case DataType::float_t: {
			if (auto v = n.value<double>()) {
				return DataValue(*v);
			}
			if (auto v = n.value<int64_t>()) {
				return DataValue(static_cast<double>(*v));
			}
			TOAST_WARN("AssetManager", "Expected float for field '{}'; defaulting to 0.0", field->name);
			return DataValue(0.0);
		}

		case DataType::string_t: {
			if (auto v = n.value<std::string_view>()) {
				return DataValue(std::string(*v));
			}
			TOAST_WARN("AssetManager", "Expected string for field '{}'; defaulting to empty", field->name);
			return DataValue(std::string {});
		}

		case DataType::node_t:
		case DataType::asset_t: {
			if (auto v = n.value<std::string_view>()) {
				if (v->size() == 11) {
					return {toast::UID(toast::UID::fromString(*v)), field->type};
				}
			}
			TOAST_WARN("AssetManager", "Expected 11-char UID for field '{}'; defaulting to zero UID", field->name);
			return DataValue(toast::UID(uint64_t {0}), field->type);
		}

		case DataType::vec2_t: {
			if (const auto* arr = n.as_array()) {
				auto nums = extractNumbers(*arr, 2);
				return DataValue(glm::vec2 {static_cast<float>(nums[0]), static_cast<float>(nums[1])});
			}
			TOAST_WARN("AssetManager", "Expected [x,y] array for vec2 field '{}'; defaulting to zero", field->name);
			return DataValue(glm::vec2 {0.f});
		}

		case DataType::vec3_t: {
			if (const auto* arr = n.as_array()) {
				auto nums = extractNumbers(*arr, 3);
				return {
				  glm::vec3 {static_cast<float>(nums[0]), static_cast<float>(nums[1]), static_cast<float>(nums[2])},
           DataType::vec3_t
				};
			}
			TOAST_WARN("AssetManager", "Expected [x,y,z] array for vec3 field '{}'; defaulting to zero", field->name);
			return {glm::vec3 {0.f}, DataType::vec3_t};
		}

		case DataType::color3_t: {
			if (const auto* arr = n.as_array()) {
				auto nums = extractNumbers(*arr, 3);
				return {
				  glm::vec3 {static_cast<float>(nums[0]), static_cast<float>(nums[1]), static_cast<float>(nums[2])},
           DataType::color3_t
				};
			}
			TOAST_WARN("AssetManager", "Expected [r,g,b] array for color3 field '{}'; defaulting to black", field->name);
			return {glm::vec3 {0.f}, DataType::color3_t};
		}

		case DataType::color4_t: {
			if (const auto* arr = n.as_array()) {
				auto nums = extractNumbers(*arr, 4);
				return DataValue(
				    glm::vec4 {
				      static_cast<float>(nums[0]), static_cast<float>(nums[1]), static_cast<float>(nums[2]), static_cast<float>(nums[3])
				    }
				);
			}
			TOAST_WARN("AssetManager", "Expected [r,g,b,a] array for color4 field '{}'; defaulting to black", field->name);
			return DataValue(glm::vec4 {0.f, 0.f, 0.f, 1.f});
		}

		case DataType::object_t: {
			const auto* tbl = n.as_table();
			if (!tbl) {
				TOAST_WARN("AssetManager", "Expected table for object field '{}'; returning empty object", field->name);
				return DataValue::makeObject();
			}
			return fromObject(*tbl, field->children);
		}

		default: return DataValue {};
	}
}

auto DataValue::fromObject(const toml::table& t, const std::vector<SchemaField>& fields) -> DataValue {
	auto obj = DataValue::makeObject();

	if (fields.empty()) {
		// Schema-less, include every key
		for (const auto& [k, v] : t) {
			obj.set(std::string(k.str()), fromToml(v, nullptr));
		}
	} else {
		// Schema-guided, emit exactly the schema keys in order
		for (const auto& f : fields) {
			auto it = t.find(f.name);
			if (it != t.end()) {
				obj.set(f.name, fromToml(it->second, &f));
			} else if (f.default_value.has_value()) {
				obj.set(f.name, *f.default_value);
			} else {
				obj.set(f.name, DataValue {});    // Null
			}
		}
	}
	return obj;
}

void DataValue::appendTo(toml::table& tbl, const std::string& key) const {
	switch (m_type) {
		case DataType::null: /* skip */ break;
		case DataType::bool_t: tbl.insert_or_assign(key, std::any_cast<bool>(m_value)); break;
		case DataType::int_t: tbl.insert_or_assign(key, std::any_cast<int64_t>(m_value)); break;
		case DataType::float_t: tbl.insert_or_assign(key, std::any_cast<double>(m_value)); break;
		case DataType::string_t: tbl.insert_or_assign(key, std::any_cast<const std::string&>(m_value)); break;
		case DataType::node_t:
		case DataType::asset_t: tbl.insert_or_assign(key, std::any_cast<toast::UID>(m_value).get()); break;

		case DataType::vec2_t: {
			auto v = std::any_cast<glm::vec2>(m_value);
			toml::array arr;
			arr.push_back(static_cast<double>(v.x));
			arr.push_back(static_cast<double>(v.y));
			tbl.insert_or_assign(key, std::move(arr));
			break;
		}
		case DataType::vec3_t:
		case DataType::color3_t: {
			auto v = std::any_cast<glm::vec3>(m_value);
			toml::array arr;
			arr.push_back(static_cast<double>(v.x));
			arr.push_back(static_cast<double>(v.y));
			arr.push_back(static_cast<double>(v.z));
			tbl.insert_or_assign(key, std::move(arr));
			break;
		}
		case DataType::color4_t: {
			auto v = std::any_cast<glm::vec4>(m_value);
			toml::array arr;
			arr.push_back(static_cast<double>(v.x));
			arr.push_back(static_cast<double>(v.y));
			arr.push_back(static_cast<double>(v.z));
			arr.push_back(static_cast<double>(v.w));
			tbl.insert_or_assign(key, std::move(arr));
			break;
		}
		case DataType::array_t: {
			toml::array arr;
			for (const auto& elem : std::any_cast<const Array&>(m_value)) {
				elem.appendTo(arr);
			}
			tbl.insert_or_assign(key, std::move(arr));
			break;
		}
		case DataType::object_t: tbl.insert_or_assign(key, asTomlTable()); break;
	}
}

void DataValue::appendTo(toml::array& arr) const {
	switch (m_type) {
		case DataType::null: /* skip */ break;
		case DataType::bool_t: arr.push_back(std::any_cast<bool>(m_value)); break;
		case DataType::int_t: arr.push_back(std::any_cast<int64_t>(m_value)); break;
		case DataType::float_t: arr.push_back(std::any_cast<double>(m_value)); break;
		case DataType::string_t: arr.push_back(std::any_cast<const std::string&>(m_value)); break;
		case DataType::node_t:
		case DataType::asset_t: arr.push_back(std::any_cast<toast::UID>(m_value).get()); break;

		case DataType::vec2_t: {
			auto v = std::any_cast<glm::vec2>(m_value);
			toml::array sub;
			sub.push_back(static_cast<double>(v.x));
			sub.push_back(static_cast<double>(v.y));
			arr.push_back(std::move(sub));
			break;
		}
		case DataType::vec3_t:
		case DataType::color3_t: {
			auto v = std::any_cast<glm::vec3>(m_value);
			toml::array sub;
			sub.push_back(static_cast<double>(v.x));
			sub.push_back(static_cast<double>(v.y));
			sub.push_back(static_cast<double>(v.z));
			arr.push_back(std::move(sub));
			break;
		}
		case DataType::color4_t: {
			auto v = std::any_cast<glm::vec4>(m_value);
			toml::array sub;
			sub.push_back(static_cast<double>(v.x));
			sub.push_back(static_cast<double>(v.y));
			sub.push_back(static_cast<double>(v.z));
			sub.push_back(static_cast<double>(v.w));
			arr.push_back(std::move(sub));
			break;
		}
		case DataType::array_t: {
			toml::array sub;
			for (const auto& elem : std::any_cast<const Array&>(m_value)) {
				elem.appendTo(sub);
			}
			arr.push_back(std::move(sub));
			break;
		}
		case DataType::object_t: arr.push_back(asTomlTable()); break;
	}
}

auto DataValue::asTomlTable() const -> toml::table {
	TOAST_ASSERT(m_type == DataType::object_t, "AssetManager", "asTomlTable() called on non-Object");
	toml::table tbl;
	for (const auto& [k, v] : std::any_cast<const ObjectMap&>(m_value)) {
		v.appendTo(tbl, k);
	}
	return tbl;
}

auto DataValue::toJson() const -> json_t {
	switch (m_type) {
		case DataType::null: return nullptr;
		case DataType::bool_t: return std::any_cast<bool>(m_value);
		case DataType::int_t: return std::any_cast<int64_t>(m_value);
		case DataType::float_t: return std::any_cast<double>(m_value);
		case DataType::string_t: return std::any_cast<const std::string&>(m_value);
		case DataType::node_t:
		case DataType::asset_t: return std::any_cast<toast::UID>(m_value).get();

		case DataType::vec2_t: {
			auto v = std::any_cast<glm::vec2>(m_value);
			return json_t::array({v.x, v.y});
		}
		case DataType::vec3_t:
		case DataType::color3_t: {
			auto v = std::any_cast<glm::vec3>(m_value);
			return json_t::array({v.x, v.y, v.z});
		}
		case DataType::color4_t: {
			auto v = std::any_cast<glm::vec4>(m_value);
			return json_t::array({v.x, v.y, v.z, v.w});
		}
		case DataType::array_t: {
			json_t arr = json_t::array();
			for (const auto& elem : std::any_cast<const Array&>(m_value)) {
				arr.push_back(elem.toJson());
			}
			return arr;
		}
		case DataType::object_t: {
			json_t obj = json_t::object();
			for (const auto& [k, v] : std::any_cast<const ObjectMap&>(m_value)) {
				obj[k] = v.toJson();
			}
			return obj;
		}
	}
	return nullptr;
}

auto DataValue::fromJson(const json_t& j, DataType hint) -> DataValue {
	if (j.is_null()) {
		return DataValue {};
	}
	if (j.is_boolean()) {
		return DataValue(j.get<bool>());
	}
	if (j.is_number_integer() && hint != DataType::float_t) {
		return DataValue(j.get<int64_t>());
	}
	if (j.is_number()) {
		return DataValue(j.get<double>());
	}
	if (j.is_string()) {
		// If the hint tells us it's a UID, reconstruct as Node
		if (hint == DataType::node_t || hint == DataType::asset_t) {
			auto s = j.get<std::string>();
			if (s.size() == 11) {
				return {toast::UID(toast::UID::fromString(s)), hint};
			}
		}
		return DataValue(j.get<std::string>());
	}
	if (j.is_array()) {
		auto result = DataValue::makeArray();
		for (const auto& elem : j) {
			result.push(fromJson(elem));
		}
		return result;
	}
	if (j.is_object()) {
		auto obj = DataValue::makeObject();
		for (const auto& [k, v] : j.items()) {
			obj.set(k, fromJson(v));
		}
		return obj;
	}
	return DataValue {};
}

}
