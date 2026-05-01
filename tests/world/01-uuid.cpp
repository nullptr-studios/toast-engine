#include <print>
#include <toast/factory.hpp>

auto main() -> int {
	std::println("\n--- Pure u64 ID ---");
	for (int i = 0; i < 100; i++) {
		std::println("{:>20}", toast::assignUuid());
	}

	std::println("\n--- Decoded Alignment ---");
	for (int i = 0; i < 100; i++) {
		auto id = toast::assignUuid();
		auto b64 = toast::uuidToString(id);

		std::println("{:>12}: {:>20}", b64, id);
	}

	return 0;
}
