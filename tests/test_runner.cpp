#include "test_registry.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>
#include <toast/world/reflect.hpp>
#include <toast/world/world_test_access.hpp>

namespace {

void print_usage(const char* exe_name) {
	std::cout << "Usage: " << exe_name << " [--list] [--test <name>]" << "\n";
}

} // namespace

int main(int argc, char** argv) {
	toast::NodeRegistry reflection_registry;
	toast::registerEngineTypes();
	toast::_detail::WorldTestAccess::initThreadPool();

	std::string_view requested_test;
	bool list_only = false;

	for (int i = 1; i < argc; ++i) {
		std::string_view arg = argv[i];
		if (arg == "--list") {
			list_only = true;
		} else if (arg == "--test") {
			if (i + 1 >= argc) {
				print_usage(argv[0]);
				return 2;
			}
			requested_test = argv[++i];
		} else if (arg == "--help" || arg == "-h") {
			print_usage(argv[0]);
			return 0;
		}
	}

	const auto& cases = toast::tests::registry();
	if (list_only) {
		for (const auto& test_case : cases) {
			std::cout << test_case.name << "\n";
		}
		return 0;
	}

	auto run_case = [](const toast::tests::TestCase& test_case) {
		std::cout << "[ RUN      ] " << test_case.name << "\n";
		test_case.fn();
		std::cout << "[     OK   ] " << test_case.name << "\n";
	};

	if (!requested_test.empty()) {
		auto it = std::find_if(cases.begin(), cases.end(), [&](const auto& test_case) {
			return test_case.name == requested_test;
		});
		if (it == cases.end()) {
			std::cerr << "Unknown test: " << requested_test << "\n";
			return 2;
		}
		run_case(*it);
		return 0;
	}

	for (const auto& test_case : cases) {
		run_case(test_case);
	}

	return 0;
}

