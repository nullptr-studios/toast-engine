/**
 * @file NodeFile.hpp
 * @author Xein
 * @date 23 May 2026
 *
 * @brief TODO: Brief description of the file's purpose
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
#include <toast/world/reflect.hpp>
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

constexpr char string_array_separator = 31;    // unit separator ␟

// Current foramt
constexpr uint16_t format_version = 2;

struct NodeFileBinaryHeader {
	const std::array<uint8_t, 6> magic = {'T', 'N', 'O', 'D', 'E', '\0'};
	uint16_t version = format_version;
	uint32_t node_count;
};
}

class Prefab : public Asset, public ISaveable {
public:
	Prefab(std::istream& file);
	Prefab(std::span<const uint8_t> bytes);
	explicit Prefab(const toast::Node& node, toast::UID self_uid = toast::UID(0));

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "node";
	}

	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	auto toFile() const -> std::string;
	auto toBinary() const -> std::vector<uint8_t>;

	auto validate() const -> bool;

	struct Field {
		std::string name;
		toast::FieldType type;
		bool is_array;
		std::any value;

		template<typename T>
		T as() const {
			return std::any_cast<T>(value);
		}
	};

	struct Subgroup {
		std::string name;
		std::vector<Field> fields;

		auto find(std::string_view name) const -> std::optional<Field> {
			auto it = std::ranges::find_if(fields, [name](auto field) { return field.name == name; });
			if (it != fields.end()) {
				return *it;
			}
			return {};
		}
	};

	struct Group {
		std::string name;
		std::vector<Field> fields;
		std::vector<Subgroup> subgroups;

		auto find(std::string_view name) const -> std::optional<Field> {
			auto it = std::ranges::find_if(fields, [&name](auto field) { return field.name == name; });
			if (it != fields.end()) {
				return *it;
			}
			for (auto& g : subgroups) {
				auto g_it = g.find(name);
				if (g_it.has_value()) {
					return g_it;
				}
			}

			return {};
		}
	};

	struct BasicNode {
		std::string name;
		std::string type;

		std::vector<Field> fields;
		std::vector<Group> groups;

		auto find(std::string_view name) const -> std::optional<Field> {
			auto it = std::ranges::find_if(fields, [&name](auto field) { return field.name == name; });
			if (it != fields.end()) {
				return *it;
			}
			for (auto& g : groups) {
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

	toast::UID m_self_uid;
	std::unordered_set<uint64_t> m_allowed_uids;
};

}
