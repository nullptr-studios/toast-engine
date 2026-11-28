/**
 * @file model.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief What the console knows about a line
 */

#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <toast/world/box.hpp>
#include <utility>
#include <vector>

namespace toast {
class Node;
}

namespace toast::console {

enum class EntryKind : uint8_t {
	group,
	subgroup,
	variable,
	value,
	command,
	node,
	field,
	function
};

struct Entry {
	std::string label;
	std::string insert;
	std::string value;
	EntryKind kind = EntryKind::variable;
};

struct Completion {
	std::vector<Entry> entries;
	size_t replace_begin = 0;
	size_t replace_end = 0;

	std::string hint;
};

struct Result {
	bool ok = true;
	std::string message;
};

class TOAST_API Model {
public:
	[[nodiscard]]
	auto complete(std::string_view line, size_t cursor) const -> Completion;

	auto execute(std::string_view line) -> Result;

	void setNodeRoot(std::function<Box<Node>()> root);

	[[nodiscard]]
	auto pinnedLines() const -> std::vector<std::string>;

	static void applyQueued();

	void setAssetCheck(std::function<bool(std::string_view)> check);

private:
	struct Pin {
		std::string spec;
		bool parameter = false;
	};

	auto pin(std::string_view spec) -> Result;
	auto hide(std::string_view spec) -> Result;
	auto completeHide(std::string_view line, size_t cursor, size_t base) const -> Completion;
	[[nodiscard]]
	auto describe(const Pin& pin) const -> std::pair<std::string, std::string>;

	std::function<bool(std::string_view)> m_asset_check;
	std::function<Box<Node>()> m_node_root;
	std::vector<Pin> m_pins;
};

}
