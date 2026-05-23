#include "node_file.hpp"

#include "toast/world/uuid.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <toast/log.hpp>
#include <sstream>
#include <istream>

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
	if (view.empty()) return std::nullopt;

	T val;
	auto [ptr, error] = std::from_chars(view.data(), view.data() + view.size(), val);

	if (error == std::errc{}) return val;
	return std::nullopt;
}

template<>
auto getValue<bool>(std::string_view view) -> std::optional<bool> {
	if (view == "true" || view == "1") return true;
	if (view == "false" || view == "0") return false;
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
	T readValue() {
		if (offset + sizeof(T) > data.size()) {
			return T{};
		}
		T value;
		std::memcpy(&value, data.data() + offset, sizeof(T));
		offset += sizeof(T);
		return value;
	}

	std::string readString() {
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

namespace toast {
NodeFile::NodeFile(std::istream& file) {
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(file, line)) {
		// Strip leading/trailing whitespaces and tabs
		size_t start = line.find_first_not_of(" \t\r\n\v\f");
		if (start == std::string::npos) continue;

		size_t end = line.find_last_not_of(" \t\r\n\v\f");
		std::string cleaned = line.substr(start, end - start + 1);
		if (cleaned.empty()) continue;

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
			// Global item
			auto item = parseItem(current);
			if (item) {
				global_items.push_back(std::move(*item));
			} else {
				TOAST_WARN("ResourceManager", "Failed to parse global item: {}", current);
			}
			i++;
		}
	}
}

auto NodeFile::toFile() -> std::string {
	std::stringstream ss;
	for (const auto& item : global_items) {
		writeItem(item, ss);
	}

	for (size_t i = 0; i < nodes.size(); ++i) {
		if (i > 0 || !global_items.empty()) {
			ss << "\n";
		}
		writeNode(nodes[i], ss);
	}

	return ss.str();
}

auto NodeFile::parseNodeChunk(std::span<const std::string> lines) -> std::optional<BasicNode> {
	if (lines.empty()) return std::nullopt;

	// Parse header: [name type=type]
	std::string_view header = lines[0];
	if (!header.starts_with('[') || !header.ends_with(']')) return std::nullopt;
	header.remove_prefix(1);
	header.remove_suffix(1);

	size_t name_end = header.find(' ');
	if (name_end == std::string_view::npos) return std::nullopt;

	std::string name{header.substr(0, name_end)};
	header.remove_prefix(name_end + 1);

	if (!header.starts_with("type=")) return std::nullopt;
	header.remove_prefix(5);
	std::string type{header};

	BasicNode node{.name = std::move(name), .type = std::move(type)};

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
				if (lines[chunk_end].starts_with('[') || 
				   (lines[chunk_end].starts_with('.') && !lines[chunk_end].starts_with(".."))) {
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
			auto item = parseItem(current);
			if (item) {
				node.items.push_back(std::move(*item));
			}
			i++;
		}
	}

	return node;
}

auto NodeFile::parseGroupChunk(std::span<const std::string> lines) -> std::optional<Group> {
	if (lines.empty()) return std::nullopt;

	// Header: .name
	std::string_view header = lines[0];
	header.remove_prefix(1);
	Group group{.name = std::string{header}};

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
			auto item = parseItem(current);
			if (item) {
				group.items.push_back(std::move(*item));
			}
			i++;
		}
	}

	return group;
}

auto NodeFile::parseSubgroupChunk(std::span<const std::string> lines) -> std::optional<Subgroup> {
	if (lines.empty()) return std::nullopt;

	// Header: ..name
	std::string_view header = lines[0];
	header.remove_prefix(2);
	Subgroup subgroup{.name = std::string{header}};

	for (size_t i = 1; i < lines.size(); i++) {
		auto item = parseItem(lines[i]);
		if (item) {
			subgroup.items.push_back(std::move(*item));
		}
	}

	return subgroup;
}

auto NodeFile::parseItem(std::string_view line) -> std::optional<Item> {
#ifndef NDEBUG
	// Safety check but it should be done before sending this
	size_t start = line.find_first_not_of(" \t\n\r\v\f");
	if (start == std::string_view::npos) {
		TOAST_WARN("ResourceManager", "NodeFile::parseItem() received a completely empty line");
		return std::nullopt;
	}

	if (start != 0) {
		TOAST_WARN("ResourceManager", "NodeFile::parseItem() didn't have a proper line (leading whitespaces)");
		line = line.substr(start);
	}
#endif

	// Get name
	size_t name_end = line.find(' ');
	if (name_end == std::string_view::npos || name_end == 0) {
		TOAST_ERROR("ResourceManager", "Malformed line: Missing space after name");
		return std::nullopt;
	}
	std::string name{line.substr(0, name_end)};
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

	return Item{
		.name = std::move(name),
		.type = *type,
		.is_array = is_array,
		.value = std::move(*value)
	};
}

auto NodeFile::parseType(std::string_view type, bool& is_array) -> std::optional<ItemType> {
	if (type.starts_with(_detail::array_str)) {
		is_array = true;
		type.remove_prefix(_detail::array_str.size());
	} else {
		is_array = false;
	}

	if (type == _detail::bool_str) return ItemType::bool_t;
	if (type == _detail::int_str) return ItemType::int_t;
	if (type == _detail::string_str) return ItemType::string_t;
	if (type == _detail::float_str) return ItemType::float_t;
	if (type == _detail::double_str) return ItemType::double_t;
	if (type == _detail::uuid_str) return ItemType::uuid_t;
	if (type == _detail::vec2_str) return ItemType::vec2_t;
	if (type == _detail::vec3_str) return ItemType::vec3_t;
	if (type == _detail::vec4_str) return ItemType::vec4_t;
	if (type == _detail::quaternion_str) return ItemType::quaternion_t;
	return std::nullopt;
}

auto NodeFile::parseValue(ItemType type, std::string_view value, bool& is_array) -> std::optional<std::any> {
	auto parse_single = [](ItemType type, std::string_view token) -> std::optional<std::any>{
		switch (type) {
			// holy boilerplate lil bro
			case ItemType::string_t:
				return std::any{std::string(token)};
			case ItemType::bool_t:
				if (auto b = getValue<bool>(token)) { return std::any{b.value()}; }
				return std::nullopt;
			case ItemType::int_t:
				if (auto b = getValue<int>(token)) { return std::any{b.value()}; }
				return std::nullopt;
			case ItemType::float_t:
				if (auto b = getValue<float>(token)) { return std::any{b.value()}; }
				return std::nullopt;
			case ItemType::double_t:
				if (auto b = getValue<double>(token)) { return std::any{b.value()}; }
				return std::nullopt;
			case ItemType::uuid_t: {
				toast::UUID id;
				id.assign(token);
				return std::any{id};
			}
			case ItemType::vec2_t: {
				std::string_view view = token;
				auto x = getValue<float>(nextValue(view));
				auto y = getValue<float>(nextValue(view));
				if (x and y) return std::any{glm::vec2{*x, *y}};
				return std::nullopt;
			}
			case ItemType::vec3_t: {
				std::string_view view = token;
				auto x = getValue<float>(nextValue(view));
				auto y = getValue<float>(nextValue(view));
				auto z = getValue<float>(nextValue(view));
				if (x and y and z) return std::any{glm::vec3{*x, *y, *z}};
				return std::nullopt;
			}
			case ItemType::vec4_t: {
				std::string_view view = token;
				auto x = getValue<float>(nextValue(view));
				auto y = getValue<float>(nextValue(view));
				auto z = getValue<float>(nextValue(view));
				auto w = getValue<float>(nextValue(view));
				if (x and y and z and w) return std::any{glm::vec4{*x, *y, *z, *w}};
				return std::nullopt;
			}
			case ItemType::quaternion_t: {
				std::string_view view = token;
				auto x = getValue<float>(nextValue(view));
				auto y = getValue<float>(nextValue(view));
				auto z = getValue<float>(nextValue(view));
				auto w = getValue<float>(nextValue(view));
				if (x and y and z and w) return std::any{glm::quat{*w, *x, *y, *z}};
				return std::nullopt;
			}
			default:
				return std::nullopt;
		}
	};

	// Parse as array
	if (is_array) {
		std::string_view remaining = value;

		if (type == ItemType::string_t) {
			std::vector<std::string> result;
			while (!remaining.empty()) {
				result.emplace_back(nextValue(remaining, _detail::string_array_separator));
			}
			return std::any {std::move(result)};
		}

		size_t tokens_per_element = 1;
		if (type == ItemType::vec2_t) {
			tokens_per_element = 2;
		} else if (type == ItemType::vec3_t) {
			tokens_per_element = 3;
		} else if (type == ItemType::vec4_t || type == ItemType::quaternion_t) {
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
			case ItemType::bool_t: return convert_to_vector.operator()<bool>();
			case ItemType::int_t: return convert_to_vector.operator()<int>();
			case ItemType::float_t: return convert_to_vector.operator()<float>();
			case ItemType::double_t: return convert_to_vector.operator()<double>();
			case ItemType::vec2_t: return convert_to_vector.operator()<glm::vec2>();
			case ItemType::vec3_t: return convert_to_vector.operator()<glm::vec3>();
			case ItemType::vec4_t: return convert_to_vector.operator()<glm::vec4>();
			case ItemType::quaternion_t: return convert_to_vector.operator()<glm::quat>();
			default: return std::nullopt;
		}
	}

	return parse_single(type, value);
}

void NodeFile::writeNode(const BasicNode& node, std::stringstream& ss) {
	ss << std::format("[{0} type={1}]\n", node.name, node.type);

	for (const auto& item : node.items) {
		writeItem(item, ss);
	}

	for (const auto& group : node.groups) {
		writeGroup(group, ss);
	}
}

void NodeFile::writeGroup(const Group& group, std::stringstream& ss) {
	ss << std::format(".{}\n", group.name);

	for (const auto& item : group.items) {
		writeItem(item, ss, "    ");
	}

	for (const auto& subgroup : group.subgroups) {
		writeSubgroup(subgroup, ss);
	}
}

void NodeFile::writeSubgroup(const Subgroup& subgroup, std::stringstream& ss) {
	ss << std::format("    ..{}\n", subgroup.name);

	for (const auto& item : subgroup.items) {
		writeItem(item, ss, "        ");
	}
}

void NodeFile::writeItem(const Item& item, std::stringstream& ss, std::string offset) {
	auto stringify_single = [](ItemType type, const std::any& value) -> std::string {
		switch (type) {
			case ItemType::string_t: return std::any_cast<std::string>(value);
			case ItemType::bool_t: return std::any_cast<bool>(value) ? "true" : "false";
			case ItemType::int_t: return std::to_string(std::any_cast<int>(value));
			case ItemType::float_t: return std::format("{}", std::any_cast<float>(value));
			case ItemType::double_t: return std::format("{}", std::any_cast<double>(value));
			case ItemType::uuid_t: return std::format("{}", std::any_cast<UUID>(value));
			case ItemType::vec2_t: {
				auto v = std::any_cast<glm::vec2>(value);
				return std::format("{} {}", v.x, v.y);
			}
			case ItemType::vec3_t: {
				auto v = std::any_cast<glm::vec3>(value);
				return std::format("{} {} {}", v.x, v.y, v.z);
			}
			case ItemType::vec4_t: {
				auto v = std::any_cast<glm::vec4>(value);
				return std::format("{} {} {} {}", v.x, v.y, v.z, v.w);
			}
			case ItemType::quaternion_t: {
				auto q = std::any_cast<glm::quat>(value);
				return std::format("{} {} {} {}", q.x, q.y, q.z, q.w);
			}
			default: return "";
		}
	};

	std::string value_str;
	if (item.is_array) {
		auto stringify_array = [&]<typename T>(ItemType type) -> std::string {
			const auto& vec = std::any_cast<const std::vector<T>&>(item.value);
			std::string result;
			for (size_t i = 0; i < vec.size(); ++i) {
				result += stringify_single(type, vec[i]);
				if (i < vec.size() - 1) {
					result += (type == ItemType::string_t) ? std::string(1, _detail::string_array_separator) : " ";
				}
			}
			return result;
		};

		switch (item.type) {
			case ItemType::bool_t: value_str = stringify_array.operator()<bool>(item.type); break;
			case ItemType::int_t: value_str = stringify_array.operator()<int>(item.type); break;
			case ItemType::float_t: value_str = stringify_array.operator()<float>(item.type); break;
			case ItemType::double_t: value_str = stringify_array.operator()<double>(item.type); break;
			case ItemType::string_t: value_str = stringify_array.operator()<std::string>(item.type); break;
			case ItemType::vec2_t: value_str = stringify_array.operator()<glm::vec2>(item.type); break;
			case ItemType::vec3_t: value_str = stringify_array.operator()<glm::vec3>(item.type); break;
			case ItemType::vec4_t: value_str = stringify_array.operator()<glm::vec4>(item.type); break;
			case ItemType::quaternion_t: value_str = stringify_array.operator()<glm::quat>(item.type); break;
			default: break;
		}
	} else {
		value_str = stringify_single(item.type, item.value);
	}

	ss << std::format("{0}{1} @{2} = {3}\n", offset, item.name, writeType(item.type, item.is_array), value_str);
}

auto NodeFile::toBinary() -> std::vector<uint8_t> {
	std::vector<uint8_t> buffer;
	_detail::NodeFileBinaryHeader header;
	header.node_count = static_cast<uint32_t>(nodes.size());
	writeBytes(buffer, &header, sizeof(header));

	auto write_item = [&](const Item& item) {
		writeString(buffer, item.name);
		writeValue(buffer, static_cast<uint8_t>(item.type));
		writeValue(buffer, static_cast<uint8_t>(item.is_array));

		auto write_single = [&](ItemType type, const std::any& value) {
			switch (type) {
				case ItemType::bool_t: writeValue(buffer, std::any_cast<bool>(value)); break;
				case ItemType::int_t: writeValue(buffer, std::any_cast<int>(value)); break;
				case ItemType::float_t: writeValue(buffer, std::any_cast<float>(value)); break;
				case ItemType::double_t: writeValue(buffer, std::any_cast<double>(value)); break;
				case ItemType::string_t: writeString(buffer, std::any_cast<std::string>(value)); break;
				case ItemType::vec2_t: writeValue(buffer, std::any_cast<glm::vec2>(value)); break;
				case ItemType::vec3_t: writeValue(buffer, std::any_cast<glm::vec3>(value)); break;
				case ItemType::vec4_t: writeValue(buffer, std::any_cast<glm::vec4>(value)); break;
				case ItemType::quaternion_t: writeValue(buffer, std::any_cast<glm::quat>(value)); break;
				default: break;
			}
		};

		if (item.is_array) {
			auto write_array = [&]<typename T>(ItemType type) {
				const auto& vec = std::any_cast<const std::vector<T>&>(item.value);
				writeValue(buffer, static_cast<uint32_t>(vec.size()));
				for (const auto& v : vec) {
					write_single(type, v);
				}
			};

			switch (item.type) {
				case ItemType::bool_t: write_array.operator()<bool>(item.type); break;
				case ItemType::int_t: write_array.operator()<int>(item.type); break;
				case ItemType::float_t: write_array.operator()<float>(item.type); break;
				case ItemType::double_t: write_array.operator()<double>(item.type); break;
				case ItemType::string_t: write_array.operator()<std::string>(item.type); break;
				case ItemType::vec2_t: write_array.operator()<glm::vec2>(item.type); break;
				case ItemType::vec3_t: write_array.operator()<glm::vec3>(item.type); break;
				case ItemType::vec4_t: write_array.operator()<glm::vec4>(item.type); break;
				case ItemType::quaternion_t: write_array.operator()<glm::quat>(item.type); break;
				default: break;
			}
		} else {
			write_single(item.type, item.value);
		}
	};

	writeValue(buffer, static_cast<uint32_t>(global_items.size()));
	for (const auto& item : global_items) {
		write_item(item);
	}

	for (const auto& node : nodes) {
		writeString(buffer, node.name);
		writeString(buffer, node.type);

		writeValue(buffer, static_cast<uint32_t>(node.items.size()));
		for (const auto& item : node.items) {
			write_item(item);
		}

		writeValue(buffer, static_cast<uint32_t>(node.groups.size()));
		for (const auto& group : node.groups) {
			writeString(buffer, group.name);
			writeValue(buffer, static_cast<uint32_t>(group.items.size()));
			for (const auto& item : group.items) {
				write_item(item);
			}
			writeValue(buffer, static_cast<uint32_t>(group.subgroups.size()));
			for (const auto& subgroup : group.subgroups) {
				writeString(buffer, subgroup.name);
				writeValue(buffer, static_cast<uint32_t>(subgroup.items.size()));
				for (const auto& item : subgroup.items) {
					write_item(item);
				}
			}
		}
	}

	return buffer;
}

NodeFile::NodeFile(std::span<const uint8_t> bytes) {
	BinaryReader reader{bytes};
	auto header = reader.readValue<_detail::NodeFileBinaryHeader>();

	static constexpr std::array<uint8_t, 6> expected_magic = {'T', 'N', 'O', 'D', 'E', '\0'};
	if (header.magic != expected_magic) {
		TOAST_ERROR("ResourceManager", "Invalid NodeFile binary magic");
		return;
	}

	auto read_item = [&]() -> Item {
		Item item;
		item.name = reader.readString();
		item.type = static_cast<ItemType>(reader.readValue<uint8_t>());
		item.is_array = static_cast<bool>(reader.readValue<uint8_t>());

		auto read_single = [&](ItemType type) -> std::any {
			switch (type) {
				case ItemType::bool_t: return reader.readValue<bool>();
				case ItemType::int_t: return reader.readValue<int>();
				case ItemType::float_t: return reader.readValue<float>();
				case ItemType::double_t: return reader.readValue<double>();
				case ItemType::string_t: return reader.readString();
				case ItemType::vec2_t: return reader.readValue<glm::vec2>();
				case ItemType::vec3_t: return reader.readValue<glm::vec3>();
				case ItemType::vec4_t: return reader.readValue<glm::vec4>();
				case ItemType::quaternion_t: return reader.readValue<glm::quat>();
				default: return std::any{};
			}
		};

		if (item.is_array) {
			uint32_t count = reader.readValue<uint32_t>();

			auto read_array = [&]<typename T>(ItemType type) -> std::any {
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

			switch (item.type) {
				case ItemType::bool_t: item.value = read_array.operator()<bool>(item.type); break;
				case ItemType::int_t: item.value = read_array.operator()<int>(item.type); break;
				case ItemType::float_t: item.value = read_array.operator()<float>(item.type); break;
				case ItemType::double_t: item.value = read_array.operator()<double>(item.type); break;
				case ItemType::string_t: item.value = read_array.operator()<std::string>(item.type); break;
				case ItemType::vec2_t: item.value = read_array.operator()<glm::vec2>(item.type); break;
				case ItemType::vec3_t: item.value = read_array.operator()<glm::vec3>(item.type); break;
				case ItemType::vec4_t: item.value = read_array.operator()<glm::vec4>(item.type); break;
				case ItemType::quaternion_t: item.value = read_array.operator()<glm::quat>(item.type); break;
				default: break;
			}
		} else {
			item.value = read_single(item.type);
		}
		return item;
	};

	uint32_t global_item_count = reader.readValue<uint32_t>();
	for (uint32_t i = 0; i < global_item_count; ++i) {
		global_items.push_back(read_item());
	}

	for (uint32_t i = 0; i < header.node_count; ++i) {
		BasicNode node;
		node.name = reader.readString();
		node.type = reader.readString();

		uint32_t item_count = reader.readValue<uint32_t>();
		for (uint32_t j = 0; j < item_count; ++j) {
			node.items.push_back(read_item());
		}

		uint32_t group_count = reader.readValue<uint32_t>();
		for (uint32_t j = 0; j < group_count; ++j) {
			Group group;
			group.name = reader.readString();
			uint32_t g_item_count = reader.readValue<uint32_t>();
			for (uint32_t k = 0; k < g_item_count; ++k) {
				group.items.push_back(read_item());
			}
			uint32_t subgroup_count = reader.readValue<uint32_t>();
			for (uint32_t k = 0; k < subgroup_count; ++k) {
				Subgroup subgroup;
				subgroup.name = reader.readString();
				uint32_t sg_item_count = reader.readValue<uint32_t>();
				for (uint32_t l = 0; l < sg_item_count; ++l) {
					subgroup.items.push_back(read_item());
				}
				group.subgroups.push_back(std::move(subgroup));
			}
			node.groups.push_back(std::move(group));
		}
		nodes.push_back(std::move(node));
	}
}

auto NodeFile::writeType(ItemType type, bool is_array) -> std::string {
	std::string str;
	switch (type) {
		case ItemType::bool_t: str = _detail::bool_str; break;
		case ItemType::int_t: str = _detail::int_str; break;
		case ItemType::string_t: str = _detail::string_str; break;
		case ItemType::float_t: str = _detail::float_str; break;
		case ItemType::double_t: str = _detail::double_str; break;
		case ItemType::uuid_t: str = _detail::uuid_str; break;
		case ItemType::vec2_t: str = _detail::vec2_str; break;
		case ItemType::vec3_t: str = _detail::vec3_str; break;
		case ItemType::vec4_t: str = _detail::vec4_str; break;
		case ItemType::quaternion_t: str = _detail::quaternion_str; break;
	}

	if (is_array) {
		str.insert(0, _detail::array_str);
	}
	return str;
}
}
