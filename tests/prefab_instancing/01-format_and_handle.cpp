#include "../../engine/src/toast/assets/prefab.hpp"
#include "test_registry.hpp"

#include <cassert>
#include <span>
#include <sstream>
#include <toast/assets/core_types.hpp>
#include <toast/uid.hpp>

using namespace toast;
using namespace assets;

// Phase 1: instance identity carried as a reflected field, format-version gating, and the
// "unresolved" AssetHandle that preserves a UID even with a null pointer.
TOAST_TEST_NAMED("prefab_instancing", "prefab_instancing/01-format_and_handle", test_prefab_instancing_01) {
	// --- Unresolved handle keeps its UID with a null pointer ---------------------------------
	{
		UID id(UID::fromString("ABCDEFGHIJK"));
		AssetHandle<Prefab> handle(nullptr, id);
		assert(not handle.hasValue());
		assert(handle.uid().data() == id.data());
		assert(handle.uid().data() != 0);

		AssetHandle<Prefab> empty;
		assert(empty.uid().data() == 0);
	}

	// --- A chunk's Prefab/UID fields survive a text round-trip --------------------------------
	{
		const char* text =
		    "~format @int = 2\n"
		    "\n"
		    "[instance type=toast::Node]\n"
		    "m_uid @uid = ABCDEFGHIJK\n"
		    "m_source_prefab @uid = LMNOPQRSTUV\n";

		std::stringstream ss(text);
		Prefab nf(ss);

		assert(nf.nodes.size() == 1);
		auto prefab_field = nf.nodes[0].find("m_source_prefab");
		assert(prefab_field.has_value());
		const uint64_t expected = UID::fromString("LMNOPQRSTUV");
		assert(prefab_field->as<UID>().data() == expected);

		// toFile re-emits the marker first, and the Prefab field survives re-parsing.
		std::string out = nf.toFile();
		assert(out.rfind("~format @int = 2", 0) == 0);    // starts with the version marker

		std::stringstream round(out);
		Prefab reparsed(round);
		assert(reparsed.nodes.size() == 1);
		assert(reparsed.nodes[0].find("m_source_prefab")->as<UID>().data() == expected);
		// The "~format" marker must not survive as a real global field.
		for (const auto& f : reparsed.global_fields) {
			assert(f.name != "~format");
		}
	}

	// --- Binary version gating ---------------------------------------------------------------
	{
		const char* text = "[n type=toast::Node]\nm_uid @uid = ABCDEFGHIJK\n";
		std::stringstream ss(text);
		Prefab nf(ss);

		std::vector<uint8_t> binary = nf.toBinary();
		assert(binary.size() >= 8);

		// version lives at offset 6 (after the 6-byte magic), little-endian u16.
		auto patched = [&](uint16_t version) {
			std::vector<uint8_t> copy = binary;
			copy[6] = static_cast<uint8_t>(version & 0xFF);
			copy[7] = static_cast<uint8_t>((version >> 8) & 0xFF);
			return copy;
		};

		Prefab v1(std::span<const uint8_t>(patched(1)));
		assert(v1.nodes.size() == 1);    // legacy version accepted

		Prefab v2(std::span<const uint8_t>(patched(2)));
		assert(v2.nodes.size() == 1);    // current version accepted

		Prefab v3(std::span<const uint8_t>(patched(3)));
		assert(v3.nodes.empty());    // future version refused
	}

	// --- validate() catches duplicate UIDs and multiple rootless chunks ----------------------
	{
		std::stringstream good("[a type=toast::Node]\nm_uid @uid = ABCDEFGHIJK\n");
		assert(Prefab(good).validate());

		// Two parentless chunks.
		std::stringstream multi_root(
		    "[a type=toast::Node]\nm_uid @uid = ABCDEFGHIJK\n"
		    "\n[b type=toast::Node]\nm_uid @uid = LMNOPQRSTUV\n"
		);
		assert(not Prefab(multi_root).validate());

		// Duplicate UID across chunks (second parented so only the UID clash trips it).
		std::stringstream dup(
		    "[a type=toast::Node]\nm_uid @uid = ABCDEFGHIJK\n"
		    "\n[b type=toast::Node]\nm_uid @uid = ABCDEFGHIJK\nm_parent @uid = ABCDEFGHIJK\n"
		);
		assert(not Prefab(dup).validate());
	}
}
