#include "../test_registry.hpp"

#include <any>
#include <cassert>
#include <cstdint>
#include <toast/reflect/reflect.hpp>

namespace {
enum class TestMode : uint8_t {
	automatic = 0,
	manual = 10,
};

struct TestObject {
	TestMode mode = TestMode::automatic;
};

struct TestModeTag {
	using type = TestMode TestObject::*;
};
}

TOAST_TEST_NAMED("reflection", "reflection/01-enum-field-access", test_reflection_01_enum_field_access) {
	using Access = toast::EnumFieldAccess<TestObject, TestMode, TestModeTag>;
	toast::_detail::Accessor<TestModeTag>::member = &TestObject::mode;

	TestObject object;
	auto initial = Access::get(&object);
	assert(std::any_cast<uint64_t>(initial) == 0);

	Access::set(&object, std::any {10});
	assert(object.mode == TestMode::manual);

	Access::set(&object, std::any {uint64_t {0}});
	assert(object.mode == TestMode::automatic);
}
