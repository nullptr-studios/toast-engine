/**
 * @file NodeFile.hpp
 * @author Xein
 * @date 23 May 2026
 *
 * @brief Serialized node tree; parsed from text (editor) or binary (game)
 *
 * nodes[0] is always the tree root
 */

#pragma once
#include "core_types.hpp"

#include <any>
#include <array>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <toast/reflect/reflect.hpp>
#include <unordered_set>
#include <vector>

namespace toast {
class Node;
}

namespace assets {

namespace _detail {
constexpr std::string_view array_str = "array_";
constexpr std::string_view bool_str = "bool";
constexpr std::string_view int_str = "int";
constexpr std::string_view float_str = "float";
constexpr std::string_view string_str = "string";
constexpr std::string_view double_str = "double";
constexpr std::string_view uid_str = "uid";
constexpr std::string_view vec2_str = "vec2";
constexpr std::string_view vec3_str = "vec3";
constexpr std::string_view vec4_str = "vec4";
constexpr std::string_view quaternion_str = "quat";

constexpr char string_array_separator =
    31;    ///< ASCII 31 (unit separator), can't appear in normal text content so it's safe as an in-field delimiter

constexpr uint16_t format_version = 2;    ///< current binary layout version

struct TOAST_API NodeFileBinaryHeader {
	const std::array<uint8_t, 6> magic = {'T', 'N', 'O', 'D', 'E', '\0'};
	uint16_t version = format_version;
	uint32_t node_count;
};
}

class TOAST_API Prefab final : public Asset, public ISaveable {
public:
	static constexpr std::string_view collection = "nodes";

	/**
	 * @brief Parses a prefab from a text (.tnode) stream
	 * @param file Open input stream positioned at the start of the file
	 * @note The text format is human-readable; missing or unknown fields are silently skipped
	 */
	Prefab(std::istream& file);

	/**
	 * @brief Parses a prefab from a binary (.tbnode) byte span
	 * @param bytes Raw file bytes; must start with the TNODE magic header
	 * @note The binary header contains a version field; a mismatch logs a warning but loading is still attempted
	 */
	Prefab(std::span<const uint8_t> bytes);

	/**
	 * @brief Serializes a live node tree into a Prefab object
	 * @param node The root node of the tree to serialize; all descendants are included
	 * @param self_uid UID of this prefab asset on disk; stored in m_self_uid to detect self-referencing loops
	 *                 during instantiation
	 */
	explicit Prefab(const toast::Node& node, toast::UID self_uid = toast::UID(0));

	Prefab() = default;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "node";
	}

	/**
	 * @brief Convenience wrapper that delegates to toFile() or toBinary() based on mode
	 * @param mode SaveMode::editor produces text; SaveMode::game produces binary
	 * @return Serialized bytes
	 */
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	/**
	 * @brief Serializes to the human-readable text format
	 * @return A string containing the full .node file content
	 */
	auto toFile() const -> std::string;

	/**
	 * @brief Serializes to the compact binary format
	 * @return The .tnode file bytes, including the magic header and version
	 */
	auto toBinary() const -> std::vector<uint8_t>;

	/**
	 * @brief Checks structural invariants
	 * @return false and logs errors if nodes is empty, any UID is duplicated, or any field has an unknown type
	 */
	auto validate() const -> bool;

	/**
	 * @brief Converts a reflected field value to its text representation
	 * @param type Serialization kind of the value
	 * @param is_array True if @p value holds a std::vector of the field type
	 * @param value The value as returned by a FieldInfo getter
	 * @return The same text encoding used by the .node text format
	 * @note Shared by the text serializer and the editor inspector so both agree on formatting
	 */
	static auto stringifyValue(toast::FieldType type, bool is_array, const std::any& value) -> std::string;

	/**
	 * @brief Parses a text value back into a typed std::any for a reflected field
	 * @param type Serialization kind to parse as
	 * @param is_array True if @p value encodes a std::vector of the field type
	 * @param value The text encoding produced by stringifyValue()
	 * @return The typed value, or std::nullopt if @p value could not be parsed
	 */
	static auto valueFromString(toast::FieldType type, bool is_array, std::string_view value) -> std::optional<std::any>;

	/**
	 * @brief One field value read from a prefab file
	 *
	 * Holds the field name, serialization type, array flag, and the deserialized value as std::any.
	 * Use as<T>() to extract the value without a manual any_cast.
	 */
	struct Field {
		std::string name;
		toast::FieldType type;
		bool is_array;
		std::any value;

		template<typename T>
		[[nodiscard]]
		auto as() const -> T {
			return std::any_cast<T>(value);
		}
	};

	/**
	 * @brief A named sub-section of fields within a group
	 *
	 * Corresponds to a [subgroup] block in the text format. Used by inspector widgets to show
	 * collapsible nested categories. find() performs a linear scan by field name.
	 */
	struct Subgroup {
		std::string name;
		std::vector<Field> fields;

		[[nodiscard]]
		auto find(std::string_view name) const -> std::optional<Field> {
			auto it = std::ranges::find_if(fields, [name](const auto& field) { return field.name == name; });
			if (it != fields.end()) {
				return *it;
			}
			return {};
		}
	};

	/**
	 * @brief A named group of fields and optional subgroups
	 *
	 * Corresponds to a [group] block in the text format. find() searches flat fields first,
	 * then descends into subgroups.
	 */
	struct Group {
		std::string name;
		std::vector<Field> fields;
		std::vector<Subgroup> subgroups;

		[[nodiscard]]
		auto find(std::string_view name) const -> std::optional<Field> {
			auto it = std::ranges::find_if(fields, [&name](const auto& field) { return field.name == name; });
			if (it != fields.end()) {
				return *it;
			}
			for (const auto& g : subgroups) {
				auto g_it = g.find(name);
				if (g_it.has_value()) {
					return g_it;
				}
			}

			return {};
		}
	};

	/**
	 * @brief One node entry in a prefab file
	 *
	 * Holds the node's fully-qualified type name, display name, top-level fields, and groups.
	 * find() searches across all levels: flat fields first, then inside each group (and its subgroups).
	 * The first entry in Prefab::nodes is always the tree root.
	 */
	struct BasicNode {
		std::string name;
		std::string type;

		std::vector<Field> fields;
		std::vector<Group> groups;

		[[nodiscard]]
		auto find(std::string_view name) const -> std::optional<Field> {
			auto it = std::ranges::find_if(fields, [&name](const auto& field) { return field.name == name; });
			if (it != fields.end()) {
				return *it;
			}
			for (const auto& g : groups) {
				auto g_it = g.find(name);
				if (g_it.has_value()) {
					return g_it;
				}
			}

			return {};
		}
	};

	std::vector<Field> global_fields;
	std::vector<BasicNode> nodes;

private:
	auto parseField(std::string_view line) -> std::optional<Field>;
	auto parseType(std::string_view type, bool& is_array) -> std::optional<toast::FieldType>;
	auto parseValue(toast::FieldType type, std::string_view value, bool& is_array) -> std::optional<std::any>;

	auto parseNodeChunk(std::span<const std::string> lines) -> std::optional<BasicNode>;
	auto parseGroupChunk(std::span<const std::string> lines) -> std::optional<Group>;
	auto parseSubgroupChunk(std::span<const std::string> lines) -> std::optional<Subgroup>;

	void serializeNode(const toast::Node& node, bool is_root);

	void writeNode(const BasicNode& node, std::stringstream& ss) const;
	void writeGroup(const Group& group, std::stringstream& ss) const;
	void writeSubgroup(const Subgroup& subgroup, std::stringstream& ss) const;
	void writeField(const Field& field, std::stringstream& ss, std::string offset = "") const;
	auto writeType(toast::FieldType type, bool is_array = false) const -> std::string;

	auto fieldEquals(toast::FieldType type, bool is_array, const std::any& a, const std::any& b) const -> bool;
	auto flattenedRootFields(const AssetHandle<Prefab>& source) const -> std::optional<BasicNode>;

	toast::UID m_self_uid;    ///< if this prefab embeds itself, this UID breaks the recursion during instantiation
	std::unordered_set<uint64_t>
	    m_allowed_uids;    ///< populated during serialization; ensures child-prefab UIDs don't collide with the parent's UID space
};

}
