#pragma once

#include <string>
#include <vector>

namespace toast::tests {

using TestFn = void (*)();

struct TestCase {
	std::string name;
	std::string group;
	TestFn fn;
};

inline std::vector<TestCase>& registry() {
	static std::vector<TestCase> cases;
	return cases;
}

struct Registrar {
	Registrar(const char* name, const char* group, TestFn fn) {
		registry().push_back(TestCase{std::string(name), std::string(group), fn});
	}
};

} // namespace toast::tests

#define TOAST_TEST_NAMED(group, name_str, fn_name) \
	static void fn_name(); \
	static const toast::tests::Registrar fn_name##_registrar(name_str, group, &fn_name); \
	static void fn_name()

