#include "test_registry.hpp"

#include <cassert>
#include <toast/input/keycodes.hpp>

TOAST_TEST_NAMED("input", "input/01-keycodes", test_input_keycodes) {
	using namespace input;

	const KeyCode space = parseKeycode("keyboard/space");
	assert(space.valid);
	assert(space.device == Device::keyboard);
	assert(space.kind == InputKind::button);

	const KeyCode f1 = parseKeycode("keyboard/f1");
	assert(f1.valid && f1.device == Device::keyboard);

	const KeyCode left = parseKeycode("mouse/left");
	assert(left.valid && left.device == Device::mouse && left.kind == InputKind::button);

	const KeyCode scroll = parseKeycode("mouse/scroll");
	assert(scroll.valid && scroll.kind == InputKind::scroll && scroll.code == 0);

	const KeyCode hscroll = parseKeycode("mouse/horizontal_scroll");
	assert(hscroll.valid && hscroll.kind == InputKind::scroll && hscroll.code == 1);

	const KeyCode cursor = parseKeycode("mouse/cursor");
	assert(cursor.valid && cursor.kind == InputKind::cursor);

	const KeyCode a = parseKeycode("controller/a");
	assert(a.valid && a.device == Device::controller && a.kind == InputKind::button);

	const KeyCode stick = parseKeycode("controller/left_stick");
	assert(stick.valid && stick.kind == InputKind::axis2d && stick.code == controller_stick_left);

	const KeyCode stick_x = parseKeycode("controller/left_stick_x");
	assert(stick_x.valid && stick_x.kind == InputKind::axis1d);

	const KeyCode trigger = parseKeycode("controller/left_trigger");
	assert(trigger.valid && trigger.kind == InputKind::axis1d);

	const KeyCode shoulder = parseKeycode("controller/left_shoulder");
	assert(shoulder.valid && shoulder.kind == InputKind::button);

	const KeyCode stick_press = parseKeycode("controller/left_stick_press");
	assert(stick_press.valid && stick_press.kind == InputKind::button);

	const KeyCode paddle = parseKeycode("controller/left_4");
	assert(paddle.valid && paddle.kind == InputKind::button);

	const KeyCode bad = parseKeycode("controller/not_a_button");
	assert(!bad.valid);
}
