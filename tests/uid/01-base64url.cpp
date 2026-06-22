#include "test_registry.hpp"

#include <cassert>
#include <string>
#include <toast/uid.hpp>

using namespace toast;

// The engine encodes UIDs with the base64url alphabet ('-' and '_'), matching the C# editor.
// fromString must still accept the legacy standard-base64 alphabet ('+' and '/') so files
// written before the migration keep decoding to the same value, and must reject garbage.
TOAST_TEST_NAMED("uid", "uid/01-base64url", test_uid_01_base64url) {
	// Round-trip across a spread of values, including ones whose encoding uses '-'/'_'.
	const uint64_t samples[] = {
	    0ull,
	    1ull,
	    0xFFFFFFFFFFFFFFFFull,
	    0x0123456789ABCDEFull,
	    0xFBFFFFFFFFFFFFFFull,    // high bits exercise the 62/63 ('-'/'_') alphabet slots
	    0xDEADBEEFCAFEBABEull,
	};

	for (uint64_t v : samples) {
		std::string encoded = UID::toString(v);
		assert(encoded.size() == 11);
		assert(UID::fromString(encoded) == v);

		// The base64url string must never contain the standard-base64 specials.
		assert(encoded.find('+') == std::string::npos);
		assert(encoded.find('/') == std::string::npos);

		// The same value written in the legacy alphabet must decode identically.
		std::string legacy = encoded;
		for (char& c : legacy) {
			if (c == '-') {
				c = '+';
			} else if (c == '_') {
				c = '/';
			}
		}
		assert(UID::fromString(legacy) == v);
	}

	// Invalid input: wrong length and out-of-alphabet characters both decode to 0.
	assert(UID::fromString("tooshort") == 0ull);
	assert(UID::fromString("waytoolongforauid") == 0ull);
	assert(UID::fromString("!!!!!!!!!!!") == 0ull);    // 11 chars, none in the alphabet
}
