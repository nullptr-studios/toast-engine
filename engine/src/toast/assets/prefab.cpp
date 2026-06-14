#include "prefab.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <istream>
#include <sstream>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/uid.hpp>
#include <toast/world/node.hpp>
#include <unordered_set>

using namespace toast;

namespace {
/**
 * Gets the first value and modifies the view so it contains the rest of them
 * @param delim Character used to separate values
 */
auto nextValue(std::string_view& view, char delim = ' ') {
	size_t pos = view.find(delim);
	if (pos == std::string_view::npos) {
		std::string_view token = view;
		view = {};
		return token;
	}

	std::string_view token = view.substr(0, pos);
	view.remove_prefix(pos + 1);
	return token;
}

template<typename T>
auto getValue(std::string_view view) -> std::optional<T> {
	if (view.empty()) {
		return std::nullopt;
	}

	T val;
	auto [ptr, error] = std::from_chars(view.data(), view.data() + view.size(), val);

	if (error == std::errc {}) {
		return val;
	}
	return std::nullopt;
}

template<>
auto getValue<bool>(std::string_view view) -> std::optional<bool> {
	if (view == "true" || view == "1") {
		return true;
	}
	if (view == "false" || view == "0") {
		return false;
	}
	return std::nullopt;
}

void writeBytes(std::vector<uint8_t>& buffer, const void* data, size_t size) {
	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	buffer.insert(buffer.end(), bytes, bytes + size);
}

template<typename T>
void writeValue(std::vector<uint8_t>& buffer, const T& value) {
	writeBytes(buffer, &value, sizeof(T));
}

void writeString(std::vector<uint8_t>& buffer, const std::string& str) {
	uint32_t length = static_cast<uint32_t>(str.size());
	writeValue(buffer, length);
	writeBytes(buffer, str.data(), length);
}

struct BinaryReader {
	std::span<const uint8_t> data;
	size_t offset = 0;

	template<typename T>
	auto readValue() -> T {
		if (offset + sizeof(T) > data.size()) {
			return T {};
		}
		T value;
		std::memcpy(&value, data.data() + offset, sizeof(T));
		offset += sizeof(T);
		return value;
	}

	auto readString() -> std::string {
		uint32_t length = readValue<uint32_t>();
		if (offset + length > data.size()) {
			return "";
		}
		std::string str(reinterpret_cast<const char*>(data.data() + offset), length);
		offset += length;
		return str;
	}
};

}

namespace assets {
Prefab::Prefab(std::istream& file) {
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(file, line)) {
		// Strip leading/trailing whitespaces and tabs
		size_t start = line.find_first_not_of(" \t\r\n\v\f");
		if (start == std::string::npos) {
			continue;
		}

		size_t end = line.find_last_not_of(" \t\r\n\v\f");
		std::string cleaned = line.substr(start, end - start + 1);
		if (cleaned.empty()) {
			continue;
		}

		lines.push_back(std::move(cleaned));
	}

	for (size_t i = 0; i < lines.size();) {
		const std::string& current = lines[i];

		if (current.starts_with('[')) {
			// Find end of node chunk
			size_t chunk_end = i + 1;
			while (chunk_end < lines.size() && !lines[chunk_end].starts_with('[')) {
				chunk_end++;
			}

			auto node = parseNodeChunk(std::span<const std::string>(&lines[i], chunk_end - i));
			if (node) {
				nodes.push_back(std::move(*node));
			} else {
				TOAST_WARN("ResourceManager", "Failed to parse node chunk starting at line {}: {}", i, current);
			}
			i = chunk_end;
		} else {
			// Global field
			auto field = parseField(current);
			if (field) {
				global_fields.push_back(std::move(*field));
			} else {
				TOAST_WARN("ResourceManager", "Failed to parse global field: {}", current);
			}
			i++;
		}
	}

	std::erase_if(global_fields, [](const Field& f) { return f.name == "~format"; });

#ifndef NDEBUG
	validate();
#endif
}

auto Prefab::serialize(SaveMode mode) const -> std::vector<uint8_t> {
	validate();
	if (mode == SaveMode::game) {
		return toBinary();
	}
	auto str = toFile();
	return std::vector<uint8_t>(str.begin(), str.end());
}

auto Prefab::toFile() const -> std::string {
	std::stringstream ss;

	ss << std::format("~format @{} = {}\n", _detail::int_str, _detail::format_version);

	for (const auto& field : global_fields) {
		writeField(field, ss);
	}

	// A blank line precedes every chunk
	for (const auto& node : nodes) {
		ss << "\n";
		writeNode(node, ss);
	}

	return ss.str();
}

auto Prefab::parseNodeChunk(std::span<const std::string> lines) -> std::optional<BasicNode> {
	if (lines.empty()) {
		return std::nullopt;
	}

	// Parse header: [name type=type]
	std::string_view header = lines[0];
	if (!header.starts_with('[') || !header.ends_with(']')) {
		return std::nullopt;
	}
	header.remove_prefix(1);
	header.remove_suffix(1);

	size_t name_end = header.find(' ');
	if (name_end == std::string_view::npos) {
		return std::nullopt;
	}

	std::string name {header.substr(0, name_end)};
	header.remove_prefix(name_end + 1);

	if (!header.starts_with("type=")) {
		return std::nullopt;
	}
	header.remove_prefix(5);
	std::string type {header};

	BasicNode node {.name = std::move(name), .type = std::move(type)};

	for (size_t i = 1; i < lines.size();) {
		const std::string& current = lines[i];

		if (current.starts_with('.')) {
			if (current.starts_with("..")) {
				// Subgroup outside of group? Skip or error
				TOAST_WARN("ResourceManager", "Subgroup {} found outside of a group in node {}", current, node.name);
				i++;
				continue;
			}

			// Find end of group chunk
			size_t chunk_end = i + 1;
			while (chunk_end < lines.size()) {
				if (lines[chunk_end].starts_with('[') || (lines[chunk_end].starts_with('.') && !lines[chunk_end].starts_with(".."))) {
					break;
				}
				chunk_end++;
			}

			auto group = parseGroupChunk(std::span<const std::string>(&lines[i], chunk_end - i));
			if (group) {
				node.groups.push_back(std::move(*group));
			}
			i = chunk_end;
		} else {
			auto field = parseField(current);
			if (field) {
				node.fields.push_back(std::move(*field));
			}
			i++;
		}
	}

	return node;
}

auto Prefab::parseGroupChunk(std::span<const std::string> lines) -> std::optional<Group> {
	if (lines.empty()) {
		return std::nullopt;
	}

	// Header: .name
	std::string_view header = lines[0];
	header.remove_prefix(1);
	Group group {.name = std::string {header}};

	for (size_t i = 1; i < lines.size();) {
		const std::string& current = lines[i];

		if (current.starts_with("..")) {
			// Find end of subgroup chunk
			size_t chunk_end = i + 1;
			while (chunk_end < lines.size() && !lines[chunk_end].starts_with("..")) {
				chunk_end++;
			}

			auto subgroup = parseSubgroupChunk(std::span<const std::string>(&lines[i], chunk_end - i));
			if (subgroup) {
				group.subgroups.push_back(std::move(*subgroup));
			}
			i = chunk_end;
		} else if (current.starts_with('.')) {
			// Should not happen if chunking is correct
			i++;
		} else {
			auto field = parseField(current);
			if (field) {
				group.fields.push_back(std::move(*field));
			}
			i++;
		}
	}

	return group;
}

auto Prefab::parseSubgroupChunk(std::span<const std::string> lines) -> std::optional<Subgroup> {
	if (lines.empty()) {
		return std::nullopt;
	}

	// Header: ..name
	std::string_view header = lines[0];
	header.remove_prefix(2);
	Subgroup subgroup {.name = std::string {header}};

	for (size_t i = 1; i < lines.size(); i++) {
		auto field = parseField(lines[i]);
		if (field) {
			subgroup.fields.push_back(std::move(*field));
		}
	}

	return subgroup;
}

auto Prefab::parseField(std::string_view line) -> std::optional<Field> {
#ifndef NDEBUG
	// Safety check but it should be done before sending this
	size_t start = line.find_first_not_of(" \t\n\r\v\f");
	if (start == std::string_view::npos) {
		TOAST_WARN("ResourceManager", "Prefab::parseItem() received a completely empty line");
		return std::nullopt;
	}

	if (start != 0) {
		TOAST_WARN("ResourceManager", "Prefab::parseItem() didn't have a proper line (leading whitespaces)");
		line = line.substr(start);
	}
#endif

	// Get name
	size_t name_end = line.find(' ');
	if (name_end == std::string_view::npos || name_end == 0) {
		TOAST_ERROR("ResourceManager", "Malformed line: Missing space after name");
		return std::nullopt;
	}
	std::string name {line.substr(0, name_end)};
	line.remove_prefix(name_end);

	// Get type
	size_t type_start = line.find('@');
	if (type_start == std::string_view::npos) {
		TOAST_ERROR("ResourceManager", "Malformed line for {}: Missing '@' character", name);
		return std::nullopt;
	}
	line.remove_prefix(type_start + 1);

	size_t type_end = line.find(' ');
	if (type_end == std::string_view::npos || type_end == 0) {
		TOAST_ERROR("ResourceManager", "Malformed line for {}: Missing space after type", name);
		return std::nullopt;
	}
	std::string_view type_str = line.substr(0, type_end);
	line.remove_prefix(type_end);

	// Get value
	size_t value_start = line.find('=');
	if (value_start == std::string_view::npos) {
		TOAST_ERROR("ResourceManager", "Malformed line for {}: Missing '=' character", name);
		return std::nullopt;
	}
	line.remove_prefix(value_start + 1);

	// Skip exactly ONE space after =
	if (!line.empty() && (line.front() == ' ')) {
		line.remove_prefix(1);
	}

	size_t val_last = line.find_last_not_of(" \t\r\n");
	std::string_view value_str = (val_last != std::string_view::npos) ? line.substr(0, val_last + 1) : line;

	// Parse data
	bool is_array = false;
	auto type = parseType(type_str, is_array);
	if (!type.has_value()) {
		TOAST_ERROR("ResourceManager", "Could not parse type {} in {}", type_str, name);
		return std::nullopt;
	}

	auto value = parseValue(*type, value_str, is_array);
	if (!value.has_value()) {
		TOAST_ERROR("ResourceManager", "Could not parse value {} in {}", value_str, name);
		return std::nullopt;
	}

	return Field {.name = std::move(name), .type = *type, .is_array = is_array, .value = std::move(*value)};
}

auto Prefab::parseType(std::string_view type, bool& is_array) -> std::optional<FieldType> {
	if (type.starts_with(_detail::array_str)) {
		is_array = true;
		type.remove_prefix(_detail::array_str.size());
	} else {
		is_array = false;
	}

	if (type == _detail::bool_str) {
		return FieldType::bool_t;
	}
	if (type == _detail::int_str) {
		return FieldType::int_t;
	}
	if (type == _detail::string_str) {
		return FieldType::string_t;
	}
	if (type == _detail::float_str) {
		return FieldType::float_t;
	}
	if (type == _detail::double_str) {
		return FieldType::double_t;
	}
	if (type == _detail::uid_str) {
		return FieldType::uid_t;
	}
	if (type == _detail::vec2_str) {
		return FieldType::vec2_t;
	}
	if (type == _detail::vec3_str) {
		return FieldType::vec3_t;
	}
	if (type == _detail::vec4_str) {
		return FieldType::vec4_t;
	}
	if (type == _detail::quaternion_str) {
		return FieldType::quaternion_t;
	}
	return std::nullopt;
}

auto Prefab::parseValue(FieldType type, std::string_view value, bool& is_array) -> std::optional<std::any> {
	auto parse_single = [](FieldType type, std::string_view token) -> std::optional<std::any> {
		switch (type) {
			// holy boilerplate lil bro
			case FieldType::string_t: return std::any {std::string(token)};
			case FieldType::bool_t:
				if (auto b = getValue<bool>(token)) {
					return std::any {b.value()};
				}
				return std::nullopt;
			case FieldType::int_t:
				if (auto b = getValue<int>(token)) {
					return std::any {b.value()};
				}
				return std::nullopt;
			case FieldType::float_t:
				if (auto b = getValue<float>(token)) {
					return std::any {b.value()};
				}
				return std::nullopt;
			case FieldType::double_t:
				if (auto b = getValue<double>(token)) {
					return std::any {b.value()};
				}
				return std::nullopt;
			case FieldType::uid_t: {
				toast::UID id;
				id.assign(token);
				return std::any {id};
			}
			case FieldType::vec2_t: {
				std::string_view view = token;
				auto x = getValue<float>(nextValue(view));
				auto y = getValue<float>(nextValue(view));
				if (x and y) {
					return std::any {
					  glm::vec2 {*x, *y}
					};
				}
				return std::nullopt;
			}
			case FieldType::vec3_t: {
				std::string_view view = token;
				auto x = getValue<float>(nextValue(view));
				auto y = getValue<float>(nextValue(view));
				auto z = getValue<float>(nextValue(view));
				if (x and y and z) {
					return std::any {
					  glm::vec3 {*x, *y, *z}
					};
				}
				return std::nullopt;
			}
			case FieldType::vec4_t: {
				std::string_view view = token;
				auto x = getValue<float>(nextValue(view));
				auto y = getValue<float>(nextValue(view));
				auto z = getValue<float>(nextValue(view));
				auto w = getValue<float>(nextValue(view));
				if (x and y and z and w) {
					return std::any {
					  glm::vec4 {*x, *y, *z, *w}
					};
				}
				return std::nullopt;
			}
			case FieldType::quaternion_t: {
				std::string_view view = token;
				auto x = getValue<float>(nextValue(view));
				auto y = getValue<float>(nextValue(view));
				auto z = getValue<float>(nextValue(view));
				auto w = getValue<float>(nextValue(view));
				if (x and y and z and w) {
					return std::any {
					  glm::quat {*w, *x, *y, *z}
					};
				}
				return std::nullopt;
			}
			default: return std::nullopt;
		}
	};

	// Parse as array
	if (is_array) {
		std::string_view remaining = value;

		if (type == FieldType::string_t) {
			std::vector<std::string> result;
			while (!remaining.empty()) {
				result.emplace_back(nextValue(remaining, _detail::string_array_separator));
			}
			return std::any {std::move(result)};
		}

		size_t tokens_per_element = 1;
		if (type == FieldType::vec2_t) {
			tokens_per_element = 2;
		} else if (type == FieldType::vec3_t) {
			tokens_per_element = 3;
		} else if (type == FieldType::vec4_t || type == FieldType::quaternion_t) {
			tokens_per_element = 4;
		}

		auto slice_next_element_token = [&remaining](size_t token_count) -> std::string_view {
			std::string_view full_view = remaining;
			size_t total_length = 0;
			for (size_t i = 0; i < token_count; ++i) {
				std::string_view part = nextValue(remaining);
				total_length += part.size();
				if (i < token_count - 1) {
					total_length += 1;
				}
			}
			return full_view.substr(0, total_length);
		};

		std::vector<std::any> parsed_anys;
		while (!remaining.empty()) {
			std::string_view element_token = slice_next_element_token(tokens_per_element);
			auto single_value = parse_single(type, element_token);
			if (!single_value.has_value()) {
				return std::nullopt;
			}
			parsed_anys.push_back(std::move(*single_value));
		}

		auto convert_to_vector = [&]<typename T> -> std::any {
			std::vector<T> vec;
			vec.reserve(parsed_anys.size());
			for (auto& a : parsed_anys) {
				vec.push_back(std::any_cast<T>(a));
			}
			return std::any(std::move(vec));
		};

		switch (type) {
			case FieldType::bool_t: return convert_to_vector.operator()<bool>();
			case FieldType::int_t: return convert_to_vector.operator()<int>();
			case FieldType::float_t: return convert_to_vector.operator()<float>();
			case FieldType::double_t: return convert_to_vector.operator()<double>();
			case FieldType::vec2_t: return convert_to_vector.operator()<glm::vec2>();
			case FieldType::vec3_t: return convert_to_vector.operator()<glm::vec3>();
			case FieldType::vec4_t: return convert_to_vector.operator()<glm::vec4>();
			case FieldType::quaternion_t: return convert_to_vector.operator()<glm::quat>();
			case FieldType::uid_t: return convert_to_vector.operator()<UID>();
			default: return std::nullopt;
		}
	}

	return parse_single(type, value);
}

void Prefab::writeNode(const BasicNode& node, std::stringstream& ss) const {
	ss << std::format("[{0} type={1}]\n", node.name, node.type);

	for (const auto& field : node.fields) {
		writeField(field, ss);
	}

	for (const auto& group : node.groups) {
		writeGroup(group, ss);
	}
}

void Prefab::writeGroup(const Group& group, std::stringstream& ss) const {
	ss << std::format(".{}\n", group.name);

	for (const auto& field : group.fields) {
		writeField(field, ss, "    ");
	}

	for (const auto& subgroup : group.subgroups) {
		writeSubgroup(subgroup, ss);
	}
}

void Prefab::writeSubgroup(const Subgroup& subgroup, std::stringstream& ss) const {
	ss << std::format("    ..{}\n", subgroup.name);

	for (const auto& field : subgroup.fields) {
		writeField(field, ss, "        ");
	}
}

void Prefab::writeField(const Field& field, std::stringstream& ss, std::string offset) const {
	auto stringify_single = [](FieldType type, const std::any& value) -> std::string {
		switch (type) {
			case FieldType::string_t: return std::any_cast<std::string>(value);
			case FieldType::bool_t: return std::any_cast<bool>(value) ? "true" : "false";
			case FieldType::int_t: return std::to_string(std::any_cast<int>(value));
			case FieldType::float_t: return std::format("{}", std::any_cast<float>(value));
			case FieldType::double_t: return std::format("{}", std::any_cast<double>(value));
			case FieldType::uid_t: return std::format("{}", std::any_cast<UID>(value));
			case FieldType::vec2_t: {
				auto v = std::any_cast<glm::vec2>(value);
				return std::format("{} {}", v.x, v.y);
			}
			case FieldType::vec3_t: {
				auto v = std::any_cast<glm::vec3>(value);
				return std::format("{} {} {}", v.x, v.y, v.z);
			}
			case FieldType::vec4_t: {
				auto v = std::any_cast<glm::vec4>(value);
				return std::format("{} {} {} {}", v.x, v.y, v.z, v.w);
			}
			case FieldType::quaternion_t: {
				auto q = std::any_cast<glm::quat>(value);
				return std::format("{} {} {} {}", q.x, q.y, q.z, q.w);
			}
			default: return "";
		}
	};

	std::string value_str;
	if (field.is_array) {
		auto stringify_array = [&]<typename T>(FieldType type) -> std::string {
			const auto& vec = std::any_cast<const std::vector<T>&>(field.value);
			std::string result;
			for (size_t i = 0; i < vec.size(); ++i) {
				result += stringify_single(type, vec[i]);
				if (i < vec.size() - 1) {
					result += (type == FieldType::string_t) ? std::string(1, _detail::string_array_separator) : " ";
				}
			}
			return result;
		};

		switch (field.type) {
			case FieldType::bool_t: value_str = stringify_array.operator()<bool>(field.type); break;
			case FieldType::int_t: value_str = stringify_array.operator()<int>(field.type); break;
			case FieldType::float_t: value_str = stringify_array.operator()<float>(field.type); break;
			case FieldType::double_t: value_str = stringify_array.operator()<double>(field.type); break;
			case FieldType::string_t: value_str = stringify_array.operator()<std::string>(field.type); break;
			case FieldType::vec2_t: value_str = stringify_array.operator()<glm::vec2>(field.type); break;
			case FieldType::vec3_t: value_str = stringify_array.operator()<glm::vec3>(field.type); break;
			case FieldType::vec4_t: value_str = stringify_array.operator()<glm::vec4>(field.type); break;
			case FieldType::quaternion_t: value_str = stringify_array.operator()<glm::quat>(field.type); break;
			case FieldType::uid_t: value_str = stringify_array.operator()<UID>(field.type); break;
			default: break;
		}
	} else {
		value_str = stringify_single(field.type, field.value);
	}

	ss << std::format("{0}{1} @{2} = {3}\n", offset, field.name, writeType(field.type, field.is_array), value_str);
}

auto Prefab::toBinary() const -> std::vector<uint8_t> {
	std::vector<uint8_t> buffer;
	_detail::NodeFileBinaryHeader header;
	header.node_count = static_cast<uint32_t>(nodes.size());
	writeBytes(buffer, &header, sizeof(header));

	auto write_field = [&](const Field& field) {
		writeString(buffer, field.name);
		writeValue(buffer, static_cast<uint8_t>(field.type));
		writeValue(buffer, static_cast<uint8_t>(field.is_array));

		auto write_single = [&](FieldType type, const std::any& value) {
			switch (type) {
				case FieldType::bool_t: writeValue(buffer, std::any_cast<bool>(value)); break;
				case FieldType::int_t: writeValue(buffer, std::any_cast<int>(value)); break;
				case FieldType::float_t: writeValue(buffer, std::any_cast<float>(value)); break;
				case FieldType::double_t: writeValue(buffer, std::any_cast<double>(value)); break;
				case FieldType::string_t: writeString(buffer, std::any_cast<std::string>(value)); break;
				case FieldType::vec2_t: writeValue(buffer, std::any_cast<glm::vec2>(value)); break;
				case FieldType::vec3_t: writeValue(buffer, std::any_cast<glm::vec3>(value)); break;
				case FieldType::vec4_t: writeValue(buffer, std::any_cast<glm::vec4>(value)); break;
				case FieldType::quaternion_t: writeValue(buffer, std::any_cast<glm::quat>(value)); break;
				case FieldType::uid_t: writeValue(buffer, std::any_cast<UID>(value).data()); break;
				default: break;
			}
		};

		if (field.is_array) {
			auto write_array = [&]<typename T>(FieldType type) {
				const auto& vec = std::any_cast<const std::vector<T>&>(field.value);
				writeValue(buffer, static_cast<uint32_t>(vec.size()));
				for (const auto& v : vec) {
					write_single(type, v);
				}
			};

			switch (field.type) {
				case FieldType::bool_t: write_array.operator()<bool>(field.type); break;
				case FieldType::int_t: write_array.operator()<int>(field.type); break;
				case FieldType::float_t: write_array.operator()<float>(field.type); break;
				case FieldType::double_t: write_array.operator()<double>(field.type); break;
				case FieldType::string_t: write_array.operator()<std::string>(field.type); break;
				case FieldType::vec2_t: write_array.operator()<glm::vec2>(field.type); break;
				case FieldType::vec3_t: write_array.operator()<glm::vec3>(field.type); break;
				case FieldType::vec4_t: write_array.operator()<glm::vec4>(field.type); break;
				case FieldType::quaternion_t: write_array.operator()<glm::quat>(field.type); break;
				case FieldType::uid_t: write_array.operator()<UID>(field.type); break;
				default: break;
			}
		} else {
			write_single(field.type, field.value);
		}
	};

	writeValue(buffer, static_cast<uint32_t>(global_fields.size()));
	for (const auto& field : global_fields) {
		write_field(field);
	}

	for (const auto& node : nodes) {
		writeString(buffer, node.name);
		writeString(buffer, node.type);

		writeValue(buffer, static_cast<uint32_t>(node.fields.size()));
		for (const auto& field : node.fields) {
			write_field(field);
		}

		writeValue(buffer, static_cast<uint32_t>(node.groups.size()));
		for (const auto& group : node.groups) {
			writeString(buffer, group.name);
			writeValue(buffer, static_cast<uint32_t>(group.fields.size()));
			for (const auto& field : group.fields) {
				write_field(field);
			}
			writeValue(buffer, static_cast<uint32_t>(group.subgroups.size()));
			for (const auto& subgroup : group.subgroups) {
				writeString(buffer, subgroup.name);
				writeValue(buffer, static_cast<uint32_t>(subgroup.fields.size()));
				for (const auto& field : subgroup.fields) {
					write_field(field);
				}
			}
		}
	}

	return buffer;
}

Prefab::Prefab(std::span<const uint8_t> bytes) {
	BinaryReader reader {bytes};
	auto header = reader.readValue<_detail::NodeFileBinaryHeader>();

	static constexpr std::array<uint8_t, 6> expected_magic = {'T', 'N', 'O', 'D', 'E', '\0'};
	if (header.magic != expected_magic) {
		TOAST_ERROR("ResourceManager", "Invalid Prefab binary magic");
		return;
	}

	if (header.version > _detail::format_version) {
		TOAST_ERROR(
		    "ResourceManager",
		    "Unsupported Prefab binary version {} (this build reads up to {})",
		    header.version,
		    _detail::format_version
		);
		return;
	}

	auto read_field = [&]() -> Field {
		Field field;
		field.name = reader.readString();
		field.type = static_cast<FieldType>(reader.readValue<uint8_t>());
		field.is_array = static_cast<bool>(reader.readValue<uint8_t>());

		auto read_single = [&](FieldType type) -> std::any {
			switch (type) {
				case FieldType::bool_t: return reader.readValue<bool>();
				case FieldType::int_t: return reader.readValue<int>();
				case FieldType::float_t: return reader.readValue<float>();
				case FieldType::double_t: return reader.readValue<double>();
				case FieldType::string_t: return reader.readString();
				case FieldType::vec2_t: return reader.readValue<glm::vec2>();
				case FieldType::vec3_t: return reader.readValue<glm::vec3>();
				case FieldType::vec4_t: return reader.readValue<glm::vec4>();
				case FieldType::quaternion_t: return reader.readValue<glm::quat>();
				case FieldType::uid_t: {
					UID id;
					id.value = reader.readValue<uint64_t>();
					return id;
				}
				default: return std::any {};
			}
		};

		if (field.is_array) {
			uint32_t count = reader.readValue<uint32_t>();

			auto read_array = [&]<typename T>(FieldType type) -> std::any {
				std::vector<T> vec;
				vec.reserve(count);
				for (uint32_t i = 0; i < count; ++i) {
					auto val = read_single(type);
					if (val.has_value()) {
						vec.push_back(std::any_cast<T>(val));
					}
				}
				return vec;
			};

			switch (field.type) {
				case FieldType::bool_t: field.value = read_array.operator()<bool>(field.type); break;
				case FieldType::int_t: field.value = read_array.operator()<int>(field.type); break;
				case FieldType::float_t: field.value = read_array.operator()<float>(field.type); break;
				case FieldType::double_t: field.value = read_array.operator()<double>(field.type); break;
				case FieldType::string_t: field.value = read_array.operator()<std::string>(field.type); break;
				case FieldType::vec2_t: field.value = read_array.operator()<glm::vec2>(field.type); break;
				case FieldType::vec3_t: field.value = read_array.operator()<glm::vec3>(field.type); break;
				case FieldType::vec4_t: field.value = read_array.operator()<glm::vec4>(field.type); break;
				case FieldType::quaternion_t: field.value = read_array.operator()<glm::quat>(field.type); break;
				case FieldType::uid_t: field.value = read_array.operator()<UID>(field.type); break;
				default: break;
			}
		} else {
			field.value = read_single(field.type);
		}
		return field;
	};

	uint32_t global_field_count = reader.readValue<uint32_t>();
	for (uint32_t i = 0; i < global_field_count; ++i) {
		global_fields.push_back(read_field());
	}

	for (uint32_t i = 0; i < header.node_count; ++i) {
		BasicNode node;
		node.name = reader.readString();
		node.type = reader.readString();

		uint32_t field_count = reader.readValue<uint32_t>();
		for (uint32_t j = 0; j < field_count; ++j) {
			node.fields.push_back(read_field());
		}

		uint32_t group_count = reader.readValue<uint32_t>();
		for (uint32_t j = 0; j < group_count; ++j) {
			Group group;
			group.name = reader.readString();
			uint32_t g_field_count = reader.readValue<uint32_t>();
			for (uint32_t k = 0; k < g_field_count; ++k) {
				group.fields.push_back(read_field());
			}
			uint32_t subgroup_count = reader.readValue<uint32_t>();
			for (uint32_t k = 0; k < subgroup_count; ++k) {
				Subgroup subgroup;
				subgroup.name = reader.readString();
				uint32_t sg_field_count = reader.readValue<uint32_t>();
				for (uint32_t l = 0; l < sg_field_count; ++l) {
					subgroup.fields.push_back(read_field());
				}
				group.subgroups.push_back(std::move(subgroup));
			}
			node.groups.push_back(std::move(group));
		}
		nodes.push_back(std::move(node));
	}

#ifndef NDEBUG
	validate();
#endif
}

Prefab::Prefab(const toast::Node& node, toast::UID self_uid) : m_self_uid(self_uid) {
	auto collect = [](this auto&& self, const toast::Node& n, std::unordered_set<uint64_t>& allowed) -> void {
		allowed.insert(n.uid().data());
		for (const auto& child : n.m_children) {
			if (child->isInstanceRoot()) {
				allowed.insert(child->uid().data());
			} else {
				self(*child, allowed);
			}
		}
	};
	collect(node, m_allowed_uids);

	// The node passed in becomes the file's root
	// It is written first and it is the only one that wont have a "parent" property
	serializeNode(node, true);
}

void Prefab::serializeNode(const toast::Node& node, bool is_root) {
	const auto* node_info = node.info();
	if (!node_info) {
		TOAST_ERROR("ResourceManager", "Cannot serialize node '{}': no reflection info attached", node.name());
		return;
	}

	// Unresolved reference
	if (node.m_unresolved_chunk) {
		nodes.push_back(*node.m_unresolved_chunk);
		return;
	}

	const uint64_t source = node.m_source_prefab.uid().data();

	// We need to handle other prefabs properly
	const bool is_reference = (not is_root && node.isInstanceRoot()) ||
	                          (is_root && node.isInstanceRoot() && m_self_uid.data() != 0 && source != m_self_uid.data());

	// For a reference chunk, only fields that differ from the prefab's root are written
	std::optional<BasicNode> base;
	if (is_reference) {
		base = flattenedRootFields(node.m_source_prefab);
	}

	BasicNode out = {
	  .name = std::string {node.name()},
	  .type = std::string {node_info->type},
	};

	auto make_field = [&](const FieldInfo* f_info) -> std::optional<Field> {
		// Ignore fields with ReadOnly attribute
		if (f_info->hasAttribute("ReadOnly")) {
			return {};
		}

		const std::string_view fname = f_info->name;
		std::any value = f_info->get(const_cast<toast::Node*>(&node));

		// Node references are stored as Box<Node> in memory but serialized as UIDs
		if (auto* box = std::any_cast<Box<toast::Node>>(&value)) {
			if (is_root && fname == "m_parent") {
				return std::nullopt;    // the file root is parentless by definition
			}
			if (not box->exists()) {
				return std::nullopt;
			}
			uint64_t target = (*box)->uid().data();
			if (not m_allowed_uids.contains(target)) {
				TOAST_WARN(
				    "ResourceManager",
				    "Field '{}' of '{}' references UID {} outside this prefab; omitting",
				    fname,
				    node.name(),
				    (*box)->uid()
				);
				return std::nullopt;
			}
			value = (*box)->uid();
		}

		if (is_reference) {
			const bool forced = (fname == "m_uid" || fname == "m_parent" || fname == "m_source_prefab");
			if (not forced && base) {
				if (auto bf = base->find(fname); bf && fieldEquals(f_info->value_type, f_info->is_array, value, bf->value)) {
					return std::nullopt;
				}
			}
		} else if (fname == "m_source_prefab") {
			return std::nullopt;
		}

		return Field {
		  .name = std::string {fname},
		  .type = f_info->value_type,
		  .is_array = f_info->is_array,
		  .value = std::move(value),
		};
	};

	// Groups are matched by name so a group split across base/derived types merges cleanly.
	auto find_or_add_group = [&out](std::string_view name) -> Group& {
		for (auto& g : out.groups) {
			if (g.name == name) {
				return g;
			}
		}
		out.groups.push_back(Group {.name = std::string {name}});
		return out.groups.back();
	};
	auto find_or_add_subgroup = [](Group& group, std::string_view name) -> Subgroup& {
		for (auto& s : group.subgroups) {
			if (s.name == name) {
				return s;
			}
		}
		group.subgroups.push_back(Subgroup {.name = std::string {name}});
		return group.subgroups.back();
	};

	node_info->forEachBaseType([&](const NodeInfo& info) {
		for (const FieldInfo* f_info : info.fields) {
			if (auto field = make_field(f_info)) {
				out.fields.push_back(std::move(*field));
			}
		}
		for (const GroupInfo& group : info.groups) {
			Group& out_group = find_or_add_group(group.name);
			for (const FieldInfo* f_info : group.fields) {
				if (auto field = make_field(f_info)) {
					out_group.fields.push_back(std::move(*field));
				}
			}
			for (const SubgroupInfo& subgroup : group.subgroups) {
				Subgroup& out_subgroup = find_or_add_subgroup(out_group, subgroup.name);
				for (const FieldInfo* f_info : subgroup.fields) {
					if (auto field = make_field(f_info)) {
						out_subgroup.fields.push_back(std::move(*field));
					}
				}
			}
		}
	});

	nodes.push_back(std::move(out));

	// we do not go inside prefabs, no need to serialize its children
	if (is_reference) {
		return;
	}

	for (const auto& child : node.m_children) {
		serializeNode(*child, false);
	}
}

auto Prefab::fieldEquals(FieldType type, bool is_array, const std::any& a, const std::any& b) const -> bool {
	try {
		auto scalar_eq = [](FieldType t, const std::any& x, const std::any& y) -> bool {
			switch (t) {
				case FieldType::bool_t: return std::any_cast<bool>(x) == std::any_cast<bool>(y);
				case FieldType::int_t: return std::any_cast<int>(x) == std::any_cast<int>(y);
				case FieldType::float_t: return std::any_cast<float>(x) == std::any_cast<float>(y);
				case FieldType::double_t: return std::any_cast<double>(x) == std::any_cast<double>(y);
				case FieldType::string_t: return std::any_cast<std::string>(x) == std::any_cast<std::string>(y);
				case FieldType::uid_t: return std::any_cast<UID>(x).data() == std::any_cast<UID>(y).data();
				case FieldType::vec2_t: return std::any_cast<glm::vec2>(x) == std::any_cast<glm::vec2>(y);
				case FieldType::vec3_t: return std::any_cast<glm::vec3>(x) == std::any_cast<glm::vec3>(y);
				case FieldType::vec4_t: return std::any_cast<glm::vec4>(x) == std::any_cast<glm::vec4>(y);
				case FieldType::quaternion_t: return std::any_cast<glm::quat>(x) == std::any_cast<glm::quat>(y);
			}
			return false;
		};

		if (not is_array) {
			return scalar_eq(type, a, b);
		}

		auto vec_eq = [&]<typename T>() -> bool {
			const auto& va = std::any_cast<const std::vector<T>&>(a);
			const auto& vb = std::any_cast<const std::vector<T>&>(b);
			if (va.size() != vb.size()) {
				return false;
			}
			for (size_t i = 0; i < va.size(); ++i) {
				if constexpr (std::is_same_v<T, UID>) {
					if (va[i].data() != vb[i].data()) {
						return false;
					}
				} else {
					if (not(va[i] == vb[i])) {
						return false;
					}
				}
			}
			return true;
		};

		switch (type) {
			case FieldType::bool_t: return vec_eq.operator()<bool>();
			case FieldType::int_t: return vec_eq.operator()<int>();
			case FieldType::float_t: return vec_eq.operator()<float>();
			case FieldType::double_t: return vec_eq.operator()<double>();
			case FieldType::string_t: return vec_eq.operator()<std::string>();
			case FieldType::uid_t: return vec_eq.operator()<UID>();
			case FieldType::vec2_t: return vec_eq.operator()<glm::vec2>();
			case FieldType::vec3_t: return vec_eq.operator()<glm::vec3>();
			case FieldType::vec4_t: return vec_eq.operator()<glm::vec4>();
			case FieldType::quaternion_t: return vec_eq.operator()<glm::quat>();
		}
		return false;
	} catch (const std::bad_any_cast&) { return false; }
}

auto Prefab::flattenedRootFields(const AssetHandle<Prefab>& source) const -> std::optional<BasicNode> {
	if (not source.hasValue()) {
		return std::nullopt;
	}

	const Prefab& prefab = source.get();
	if (prefab.nodes.empty()) {
		return std::nullopt;
	}

	auto root_ref = [](const BasicNode& chunk) -> uint64_t {
		if (auto f = chunk.find("m_source_prefab")) {
			try {
				return f->as<UID>().data();
			} catch (const std::bad_any_cast&) { }
		}
		return 0;
	};

	BasicNode flat = prefab.nodes[0];

	// Walk up the variant chain
	std::unordered_set<uint64_t> seen {source.uid().data()};
	uint64_t parent_uid = root_ref(flat);
	while (parent_uid != 0 && seen.insert(parent_uid).second) {
		auto parent = assets::load<assets::Prefab>(toast::UID(parent_uid));
		if (not parent.hasValue() || parent->nodes.empty()) {
			break;
		}
		const BasicNode& parent_root = parent->nodes[0];
		for (const Field& pf : parent_root.fields) {
			if (not flat.find(pf.name).has_value()) {
				flat.fields.push_back(pf);
			}
		}
		parent_uid = root_ref(parent_root);
	}

	return flat;
}

auto Prefab::validate() const -> bool {
	std::unordered_set<uint64_t> seen_uids;
	int rootless_count = 0;
	bool ok = true;

	for (size_t i = 0; i < nodes.size(); ++i) {
		const BasicNode& node = nodes[i];

		// UID uniqueness across chunks
		if (auto uid_field = node.find("m_uid")) {
			try {
				uint64_t id = uid_field->as<UID>().data();
				if (id != 0 && not seen_uids.insert(id).second) {
					TOAST_WARN("ResourceManager", "Duplicate chunk UID {} in prefab (chunk '{}')", uid_field->as<UID>(), node.name);
					ok = false;
				}
			} catch (const std::bad_any_cast&) { }
		}

		// Exactly one parentless chunk, and it must come first
		if (not node.find("m_parent").has_value()) {
			++rootless_count;
			if (i != 0) {
				TOAST_WARN("ResourceManager", "Parentless chunk '{}' is at index {} (the root must be first)", node.name, i);
				ok = false;
			}
		}
	}

	if (rootless_count != 1) {
		TOAST_WARN("ResourceManager", "Prefab has {} parentless chunks (expected exactly 1)", rootless_count);
		ok = false;
	}

	return ok;
}

auto Prefab::writeType(FieldType type, bool is_array) const -> std::string {
	std::string str;
	switch (type) {
		case FieldType::bool_t: str = _detail::bool_str; break;
		case FieldType::int_t: str = _detail::int_str; break;
		case FieldType::string_t: str = _detail::string_str; break;
		case FieldType::float_t: str = _detail::float_str; break;
		case FieldType::double_t: str = _detail::double_str; break;
		case FieldType::uid_t: str = _detail::uid_str; break;
		case FieldType::vec2_t: str = _detail::vec2_str; break;
		case FieldType::vec3_t: str = _detail::vec3_str; break;
		case FieldType::vec4_t: str = _detail::vec4_str; break;
		case FieldType::quaternion_t: str = _detail::quaternion_str; break;
	}

	if (is_array) {
		str.insert(0, _detail::array_str);
	}
	return str;
}
}
