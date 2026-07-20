#include "test_registry.hpp"

#include <cassert>
#include <toast/assets/core_types.hpp>

namespace {

class TestAsset final : public assets::Asset {
public:
	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "test";
	}
};

}

TOAST_TEST_NAMED("Assets", "assets/01-handle-callback", test_assets_01_handle_callback) {
	TestAsset first;
	TestAsset second;
	assets::Handle<TestAsset> handle(&first);
	int changes = 0;
	handle.onChangeCallback([&changes] { ++changes; });

	const assets::Handle<TestAsset> replacement(&second);
	handle = replacement;
	assert(changes == 1);
	assert(handle.hasValue());

	const assets::Handle<TestAsset> empty;
	handle = empty;
	assert(changes == 2);
	assert(!handle.hasValue());
}
