/**
 * @file bind_value.hpp
 * @author Xein
 * @date 22 Nov 2026
 *
 * @brief Tree value behind a Panel bind
 */

#pragma once
#include <RmlUi/Core/Variant.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ui {

class BindValue {
public:
	using Array = std::vector<BindValue>;
	using Object = std::vector<std::pair<std::string, BindValue>>;

	BindValue() = default;

	[[nodiscard]]
	static auto scalar(Rml::Variant value) -> BindValue {
		BindValue v;
		v.m_data = std::move(value);
		return v;
	}

	[[nodiscard]]
	static auto array() -> BindValue {
		BindValue v;
		v.m_data = Array {};
		return v;
	}

	[[nodiscard]]
	static auto object() -> BindValue {
		BindValue v;
		v.m_data = Object {};
		return v;
	}

	[[nodiscard]]
	auto isScalar() const -> bool {
		return std::holds_alternative<Rml::Variant>(m_data);
	}

	[[nodiscard]]
	auto isArray() const -> bool {
		return std::holds_alternative<Array>(m_data);
	}

	[[nodiscard]]
	auto isObject() const -> bool {
		return std::holds_alternative<Object>(m_data);
	}

	[[nodiscard]]
	auto value() const -> const Rml::Variant& {
		return std::get<Rml::Variant>(m_data);
	}

	[[nodiscard]]
	auto items() -> Array& {
		return std::get<Array>(m_data);
	}

	[[nodiscard]]
	auto items() const -> const Array& {
		return std::get<Array>(m_data);
	}

	[[nodiscard]]
	auto members() -> Object& {
		return std::get<Object>(m_data);
	}

	[[nodiscard]]
	auto members() const -> const Object& {
		return std::get<Object>(m_data);
	}

	[[nodiscard]]
	auto find(std::string_view key) -> BindValue* {
		if (!isObject()) {
			return nullptr;
		}
		for (auto& [name, value] : members()) {
			if (name == key) {
				return &value;
			}
		}
		return nullptr;
	}

	[[nodiscard]]
	auto find(std::string_view key) const -> const BindValue* {
		return const_cast<BindValue*>(this)->find(key);    // NOLINT(cppcoreguidelines-pro-type-const-cast)
	}

	void set(std::string_view key, BindValue value) {
		if (BindValue* existing = find(key)) {
			*existing = std::move(value);
			return;
		}
		members().emplace_back(std::string(key), std::move(value));
	}

	void push(BindValue value) { items().push_back(std::move(value)); }

private:
	std::variant<Rml::Variant, Array, Object> m_data;
};

}
