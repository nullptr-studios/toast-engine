#include "test_registry.hpp"

#include <any>
#include <cassert>
#include <toast/assets/input_layout.hpp>
#include <toast/world/player_controller.hpp>
#include <toast/world/reflect.hpp>
#include <vector>

TOAST_TEST_NAMED("input", "input/06-reflect_layouts", test_input_reflect_layouts) {
	const toast::NodeInfo* info = toast::NodeRegistry::reflect("input::PlayerController");
	assert(info != nullptr);

	const toast::FieldInfo* field = info->getField("layouts");
	assert(field != nullptr);
	assert(field->value_type == toast::FieldType::uid_t);
	assert(field->is_array);

	input::PlayerController controller;
	controller.layouts.emplace_back(nullptr, toast::UID(11));
	controller.layouts.emplace_back(nullptr, toast::UID(22));

	std::any value = field->get(&controller);
	auto uids = std::any_cast<std::vector<toast::UID>>(value);
	assert(uids.size() == 2);
	assert(uids[0].data() == 11);
	assert(uids[1].data() == 22);
}
