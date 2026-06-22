#include "uid.hpp"

#include "toast/log.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <string_view>

namespace {
constexpr auto charset = std::to_array("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
std::atomic<uint64_t> offset = 0;
}

namespace toast {

auto UID::toString(uint64_t uid) -> std::string {
	// on uid = 0 this will return "AAAAAAAAAAA" (A x11 times)

	std::string result;
	result.reserve(11);

	// extract bytes
	std::array<uint8_t, 8> b;
	for (int i = 0; i < 8; ++i) {
		b[i] = static_cast<uint8_t>(uid >> (56 - (i * 8)));
	}

	result += charset[b[0] >> 2];
	result += charset[((b[0] & 0x03) << 4) | (b[1] >> 4)];
	result += charset[((b[1] & 0x0f) << 2) | (b[2] >> 6)];
	result += charset[b[2] & 0x3f];

	result += charset[b[3] >> 2];
	result += charset[((b[3] & 0x03) << 4) | (b[4] >> 4)];
	result += charset[((b[4] & 0x0f) << 2) | (b[5] >> 6)];
	result += charset[b[5] & 0x3f];

	result += charset[b[6] >> 2];
	result += charset[((b[6] & 0x03) << 4) | (b[7] >> 4)];
	result += charset[(b[7] & 0x0f) << 2];

	return result;
}

auto UID::fromString(std::string_view b64) -> uint64_t {
	static constexpr auto rev_table = []() {
		std::array<uint8_t, 256> table {};
		table.fill(0xFF);
		for (uint8_t i = 0; i < 64; ++i) {
			table[static_cast<uint8_t>(charset[i])] = i;
		}
		// just in case convert from standard b64 to b64url
		table[static_cast<uint8_t>('+')] = 62;
		table[static_cast<uint8_t>('/')] = 63;
		return table;
	}();

	if (b64.size() != 11) {
		TOAST_ERROR("UID", "Error parsing UID {}", b64);
		return 0;
	}

	std::array<uint64_t, 11> b {};
	for (size_t i = 0; i < 11; ++i) {
		b[i] = rev_table[static_cast<uint8_t>(b64[i])];
		if (b[i] == 0xFF) {
			TOAST_ERROR("UID", "Invalid character in UID {}", b64);
			return 0;
		}
	}

	const uint64_t b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3], b4 = b[4], b5 = b[5];
	const uint64_t b6 = b[6], b7 = b[7], b8 = b[8], b9 = b[9], b10 = b[10];

	uint64_t uid = 0;
	uid |= (b0 << 2 | b1 >> 4) << 56;
	uid |= ((b1 & 0xF) << 4 | b2 >> 2) << 48;
	uid |= ((b2 & 0x3) << 6 | b3) << 40;
	uid |= (b4 << 2 | b5 >> 4) << 32;
	uid |= ((b5 & 0xF) << 4 | b6 >> 2) << 24;
	uid |= ((b6 & 0x3) << 6 | b7) << 16;
	uid |= (b8 << 2 | b9 >> 4) << 8;
	uid |= ((b9 & 0xF) << 4 | b10 >> 2);

	return uid;
}

void UID::generate() {
	using namespace std::chrono;
	auto t = static_cast<uint64_t>(high_resolution_clock::now().time_since_epoch().count());

	// magic
	t = (t ^ (t >> 30)) * 0xbf58476d1ce4e5b9ULL;
	t = (t ^ (t >> 27)) * 0x94d049bb133111ebULL;
	t = (t ^ (t >> 31));

	value = t + offset;
	offset++;
}

void UID::assign(std::string_view b64) {
	value = fromString(b64);
}

auto UID::make() -> UID {
	UID u;
	u.generate();
	return u;
}

UID::UID() {
	value = 0;
}

UID::operator std::string() const noexcept {
	return get();
}

auto UID::operator<=>(const UID& other) const noexcept -> std::strong_ordering {
	return value <=> other.value;
}

auto UID::get() const noexcept -> std::string {
	return toString(value);
}

auto UID::data() const noexcept -> uint64_t {
	return value;
}
}
