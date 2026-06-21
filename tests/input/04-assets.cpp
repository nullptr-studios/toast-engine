#include "test_registry.hpp"

#include <cassert>
#include <format>
#include <toast/assets/input_action.hpp>
#include <toast/assets/input_layout.hpp>
#include <toml++/toml.hpp>

TOAST_TEST_NAMED("input", "input/04-assets", test_input_assets) {
	{
		toml::table tbl = toml::parse(
		    "name = \"jump\"\n"
		    "function_name = \"onJump\"\n"
		    "description = \"makes the player jump\"\n"
		    "type = \"Action2D\"\n"
		    "accumulation = true\n"
		);
		assets::Action action(std::move(tbl));
		assert(action.name() == "jump");
		assert(action.functionName() == "onJump");
		assert(action.valueType() == assets::ActionValueType::action_2d);
		assert(action.accumulation() == assets::AccumulationType::highest);
	}

	{
		const std::string uid_str = toast::UID::toString(424242);
		const std::string toml_str = std::format(
		    "name = \"Combat\"\n"
		    "layers = [ \"combat\", \"menu\" ]\n"
		    "[[action]]\n"
		    "id = \"{}\"\n"
		    "included = [ \"combat\" ]\n",
		    uid_str
		);

		toml::table tbl = toml::parse(toml_str);
		assets::InputLayout layout(std::move(tbl));

		assert(layout.name() == "Combat");
		assert(layout.layers().size() == 3);
		assert(layout.layers()[0] == assets::InputLayout::default_layer);

		assert(layout.entries().size() == 1);
		const auto& entry = layout.entries().front();
		assert(entry.id.data() == 424242);

		assert(assets::InputLayout::isActiveForLayer(entry, "combat"));
		assert(!assets::InputLayout::isActiveForLayer(entry, "menu"));
		assert(!assets::InputLayout::isActiveForLayer(entry, "default"));
	}

	{
		const std::string uid_str = toast::UID::toString(7);
		const std::string toml_str = std::format(
		    "name = \"Global\"\n"
		    "layers = [ \"menu\" ]\n"
		    "[[action]]\n"
		    "id = \"{}\"\n"
		    "excluded = [ \"menu\" ]\n",
		    uid_str
		);

		toml::table tbl = toml::parse(toml_str);
		assets::InputLayout layout(std::move(tbl));
		const auto& entry = layout.entries().front();

		assert(assets::InputLayout::isActiveForLayer(entry, "default"));
		assert(!assets::InputLayout::isActiveForLayer(entry, "menu"));
	}
}
