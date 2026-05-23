/**
 * @file NodeFile.hpp
 * @author Xein
 * @date 23 May 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include <any>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toast {
class Node;

namespace _detail {
	constexpr std::string_view array_str = "array_";
	constexpr std::string_view bool_str = "bool";
	constexpr std::string_view int_str = "int";
	constexpr std::string_view float_str = "float";
	constexpr std::string_view string_str = "string";
	constexpr std::string_view double_str = "double";
	constexpr std::string_view uuid_str = "uuid";
	constexpr std::string_view vec2_str = "vec2";
	constexpr std::string_view vec3_str = "vec3";
	constexpr std::string_view vec4_str = "vec4";
	constexpr std::string_view quaternion_str = "quat";

	constexpr char string_array_separator = 31; // unit separator ␟

	struct NodeFileBinaryHeader {
		const std::array<uint8_t, 6> magic = {'T', 'N', 'O', 'D', 'E', '\0'};
		uint16_t version = 1;
		uint32_t node_count;
	};
}

class NodeFile {
public:
	NodeFile(std::istream& file);
	NodeFile(std::span<const uint8_t> bytes);
	NodeFile(const Node& node);

	auto toFile() -> std::string;
	auto toBinary() -> std::vector<uint8_t>;

	enum class ItemType : uint8_t {
		bool_t,
		int_t,
		float_t,
		string_t,
		double_t,
		uuid_t,
		vec2_t,
		vec3_t,
		vec4_t,
		quaternion_t,
	};

	struct Item {
		std::string name;
		ItemType type;
		bool is_array;
		std::any value;
	};

	struct Subgroup {
		std::string name;
		std::vector<Item> items;
	};

	struct Group {
		std::string name;
		std::vector<Item> items;
		std::vector<Subgroup> subgroups;
	};

	struct BasicNode {
		std::string name;
		std::string type;

		std::vector<Item> items;
		std::vector<Group> groups;
	};

	std::vector<Item> global_items;
	std::vector<BasicNode> nodes;

private:
	auto parseItem(std::string_view line) -> std::optional<Item>;
	auto parseType(std::string_view type, bool& is_array) -> std::optional<ItemType>;
	auto parseValue(ItemType type, std::string_view value, bool& is_array) -> std::optional<std::any>;

	auto parseNodeChunk(std::span<const std::string> lines) -> std::optional<BasicNode>;
	auto parseGroupChunk(std::span<const std::string> lines) -> std::optional<Group>;
	auto parseSubgroupChunk(std::span<const std::string> lines) -> std::optional<Subgroup>;

	void writeNode(const BasicNode& node, std::stringstream& ss);
	void writeGroup(const Group& group, std::stringstream& ss);
	void writeSubgroup(const Subgroup& subgroup, std::stringstream& ss);
	void writeItem(const Item& item, std::stringstream& ss, std::string offset = "");
	auto writeType(ItemType type, bool is_array = false) -> std::string;
};

}
