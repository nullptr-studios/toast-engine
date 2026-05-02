#include "toast/world/node.hpp"
#include <cassert>
#include <print>

void func(toast::Node& node) {
	std::println("Heyllo");
}

auto main() -> int {
	toast::Box<toast::Node> node = new toast::Node;
	func(node);
}
