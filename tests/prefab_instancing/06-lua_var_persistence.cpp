#include "prefab_test_helpers.hpp"
#include "scripting/scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <cassert>
#include <cstring>
#include <sstream>
#include <toast/scripting/script_runtime.hpp>

using namespace toast;
using namespace toast::tests;
using namespace toast::tests::scripting_tests;
using assets::Prefab;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

constexpr const char* SCRIPT_SOURCE = R"lua(
local M = {}
M.health = 100
M.tags = { 1, 2, 3 }
M.target = Node
return M
)lua";

// Writes a length-prefixed string matching Prefab's private writeString() encoding
void appendString(std::vector<uint8_t>& buffer, const std::string& str) {
	const auto length = static_cast<uint32_t>(str.size());
	const auto* len_bytes = reinterpret_cast<const uint8_t*>(&length);
	buffer.insert(buffer.end(), len_bytes, len_bytes + sizeof(length));
	buffer.insert(buffer.end(), str.begin(), str.end());
}

}    // namespace

TOAST_TEST_NAMED(
    "prefab_instancing", "prefab_instancing/06-lua_var_persistence", test_prefab_instancing_06_lua_var_persistence
) {
	luaState();
	auto world = WorldTestAccess::createWorld();

	auto node = WorldTestAccess::createNode(*world, "scripted");
	auto sibling = WorldTestAccess::createNode(*world, "sibling");

	WorldTestAccess::attachScript(*node, makeScript(SCRIPT_SOURCE));

	auto* rt = node->scriptRuntime();
	assert(rt != nullptr);
	assert(rt->instanceCount() == 1);

	// Edit health and target; leave tags at its script-declared default
	assert(rt->setVarByPath(0, "health", std::any {50}));
	assert(rt->setVarByPath(0, "target", std::any {sibling}));

	// --- Serialize: only the edited vars should be diffed in --------------------------------
	Prefab saved(*node, UID(0));
	assert(saved.nodes.size() == 1);
	const auto& out = saved.nodes[0];

	const auto* health_ov = out.findLuaVar("0:health");
	assert(health_ov != nullptr);
	assert(health_ov->value == "50");

	assert(out.findLuaVar("0:tags") == nullptr);    // unchanged from default, not written

	const auto* target_ov = out.findLuaVar("0:target");
	assert(target_ov != nullptr);
	assert(target_ov->value == sibling->uid().get());

	assert(out.lua_vars.size() == 2);

	// --- Text round-trip ----------------------------------------------------------------------
	{
		std::string text = saved.toFile();
		std::stringstream ss(text);
		Prefab reloaded(ss);
		assert(reloaded.nodes.size() == 1);
		assert(reloaded.nodes[0].lua_vars.size() == 2);
		const auto* h = reloaded.nodes[0].findLuaVar("0:health");
		assert(h != nullptr && h->value == "50");
		const auto* t = reloaded.nodes[0].findLuaVar("0:target");
		assert(t != nullptr && t->value == sibling->uid().get());
	}

	// --- Binary round-trip ----------------------------------------------------------------------
	{
		std::vector<uint8_t> bin = saved.toBinary();
		std::span<const uint8_t> bin_span(bin);
		Prefab reloaded(bin_span);
		assert(reloaded.nodes.size() == 1);
		assert(reloaded.nodes[0].lua_vars.size() == 2);
		const auto* h = reloaded.nodes[0].findLuaVar("0:health");
		assert(h != nullptr && h->value == "50");
		const auto* t = reloaded.nodes[0].findLuaVar("0:target");
		assert(t != nullptr && t->value == sibling->uid().get());
	}

	// --- Reapplication: a fresh ScriptRuntime should pick up the saved overrides --------------
	{
		auto node2 = WorldTestAccess::createNode(*world, "scripted2");
		WorldTestAccess::attachScript(*node2, makeScript(SCRIPT_SOURCE));

		auto find_node = [&](std::string_view uid_text) -> Box<Node> {
			if (uid_text.empty()) {
				return {};
			}
			const uint64_t id = UID::fromString(std::string(uid_text));
			return id == sibling->uid().data() ? sibling : Box<Node> {};
		};

		WorldTestAccess::applyLuaOverrides(*world, *node2, out, find_node);

		auto* rt2 = node2->scriptRuntime();
		assert(rt2 != nullptr);

		std::any health = rt2->getVarByPath(0, "health");
		assert(std::any_cast<int>(health) == 50);

		std::any tags = rt2->getVarByPath(0, "tags");
		auto tags_vec = std::any_cast<std::vector<int>>(tags);
		assert((tags_vec == std::vector<int> {1, 2, 3}));    // untouched, still the declared default

		std::any target = rt2->getVarByPath(0, "target");
		auto target_box = std::any_cast<Box<Node>>(target);
		assert(target_box.exists());
		assert(target_box->uid().data() == sibling->uid().data());
	}

	// --- Binary backward compatibility: a hand-built pre-lua_vars (version 2) file must still
	// parse cleanly, with no lua_vars section attempted --------------------------------------
	{
		assets::_detail::NodeFileBinaryHeader header;
		header.version = 2;
		header.node_count = 1;

		std::vector<uint8_t> legacy;
		const auto* header_bytes = reinterpret_cast<const uint8_t*>(&header);
		legacy.insert(legacy.end(), header_bytes, header_bytes + sizeof(header));

		uint32_t zero = 0;
		const auto* zero_bytes = reinterpret_cast<const uint8_t*>(&zero);
		auto appendU32 = [&](uint32_t v) {
			const auto* b = reinterpret_cast<const uint8_t*>(&v);
			legacy.insert(legacy.end(), b, b + sizeof(v));
		};

		appendU32(0);    // global_field_count
		appendString(legacy, "legacy node");
		appendString(legacy, "toast::Node");
		appendU32(0);    // field_count
		// no lua_var_count section — this is what a genuine pre-Feature-E v2 writer produced
		appendU32(0);    // group_count
		(void)zero_bytes;

		std::span<const uint8_t> legacy_span(legacy);
		Prefab legacy_loaded(legacy_span);
		assert(legacy_loaded.nodes.size() == 1);
		assert(legacy_loaded.nodes[0].lua_vars.empty());
		assert(legacy_loaded.nodes[0].name == "legacy node");
	}
}
