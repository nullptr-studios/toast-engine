#include "bind_definitions.hpp"

namespace ui {

namespace {

auto asValue(void* ptr) -> BindValue* {
	return static_cast<BindValue*>(ptr);
}

auto childOf(BindValue& value) -> Rml::DataVariable {
	return {bindDefinitionFor(value), &value};
}

class ScalarDefinition final : public Rml::VariableDefinition {
public:
	ScalarDefinition() : VariableDefinition(Rml::DataVariableType::Scalar) { }

	auto Get(void* ptr, Rml::Variant& variant) -> bool override {
		const BindValue* value = asValue(ptr);
		if (value == nullptr || !value->isScalar()) {
			return false;
		}
		variant = value->value();
		return true;
	}

	auto Set(void* ptr, const Rml::Variant& variant) -> bool override {
		BindValue* value = asValue(ptr);
		if (value == nullptr || !value->isScalar()) {
			return false;
		}
		*value = BindValue::scalar(variant);
		return true;
	}
};

class ArrayDefinition final : public Rml::VariableDefinition {
public:
	ArrayDefinition() : VariableDefinition(Rml::DataVariableType::Array) { }

	auto Size(void* ptr) -> int override {
		const BindValue* value = asValue(ptr);
		return value != nullptr && value->isArray() ? static_cast<int>(value->items().size()) : 0;
	}

	auto Child(void* ptr, const Rml::DataAddressEntry& address) -> Rml::DataVariable override {
		BindValue* value = asValue(ptr);
		if (value == nullptr || !value->isArray()) {
			return {};
		}
		auto& items = value->items();
		if (address.index < 0) {
			// same as native Rml arrays: `list.size`
			return address.name == "size" ? Rml::MakeLiteralIntVariable(static_cast<int>(items.size())) : Rml::DataVariable {};
		}
		if (static_cast<size_t>(address.index) >= items.size()) {
			return {};
		}
		return childOf(items[static_cast<size_t>(address.index)]);
	}
};

class ObjectDefinition final : public Rml::VariableDefinition {
public:
	ObjectDefinition() : VariableDefinition(Rml::DataVariableType::Struct) { }

	auto Child(void* ptr, const Rml::DataAddressEntry& address) -> Rml::DataVariable override {
		BindValue* value = asValue(ptr);
		if (value == nullptr || address.index >= 0) {
			return {};
		}
		BindValue* member = value->find(address.name);
		return member != nullptr ? childOf(*member) : Rml::DataVariable {};
	}
};

}

auto bindArrayDefinition() -> Rml::VariableDefinition* {
	static ArrayDefinition definition;
	return &definition;
}

auto bindObjectDefinition() -> Rml::VariableDefinition* {
	static ObjectDefinition definition;
	return &definition;
}

auto bindDefinitionFor(const BindValue& value) -> Rml::VariableDefinition* {
	static ScalarDefinition scalar;
	if (value.isArray()) {
		return bindArrayDefinition();
	}
	if (value.isObject()) {
		return bindObjectDefinition();
	}
	return &scalar;
}

}
