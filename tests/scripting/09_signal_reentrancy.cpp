#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/events/signals.hpp"
#include "toast/world/node.hpp"

#include <cassert>

using namespace toast::tests::scripting_tests;

// Handlers may connect, disconnect, clear and fire the signal they are being fired from. Each fire runs against a
// snapshot, so nothing that happens inside it can invalidate the iteration
TOAST_TEST_NAMED("Scripting", "scripting/09_signal_reentrancy", test_scripting_09_signal_reentrancy) {
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto node = toast::_detail::WorldTestAccess::createNode(*world_owner, "host");

	// connecting while firing: the new handler waits for the next fire
	{
		signals::Signal<> signal;
		int calls = 0;
		signal.connect(*node, [&] {
			++calls;
			signal.connect(*node, [&] { ++calls; });
		});
		signal.fire();
		assert(calls == 1);
		signal.fire();
		assert(calls == 3);
	}

	// clearing while firing
	{
		signals::Signal<> signal;
		int calls = 0;
		signal.connect(*node, [&] {
			++calls;
			signal.clear(signals::ConnectionSource::cpp);
		});
		signal.connect(*node, [&] { ++calls; });
		signal.fire();
		assert(calls == 2);    // the snapshot still holds both handlers
		signal.fire();
		assert(calls == 2);    // and nothing is left afterwards
		assert(signal.connections().empty());
	}

	// firing from inside a handler (bounded by the handler itself)
	{
		signals::Signal<> signal;
		int depth = 0;
		int calls = 0;
		signal.connect(*node, [&] {
			++calls;
			if (++depth < 5) {
				signal.fire();
			}
			--depth;
		});
		signal.fire();
		assert(calls == 5);
	}

	// copies keep their own connection lists
	{
		signals::Signal<> signal;
		int calls = 0;
		signal.connect(*node, [&] { ++calls; });
		signals::Signal<> copy = signal;
		signal.clear(signals::ConnectionSource::cpp);
		copy.fire();
		signal.fire();
		assert(calls == 1);
	}
}
