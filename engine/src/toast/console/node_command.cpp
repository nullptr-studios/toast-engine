#include "node_command.hpp"

#include "fuzzy.hpp"
#include "ranking.hpp"
#include "relative.hpp"

#include <algorithm>
#include <any>
#include <cctype>
#include <charconv>
#include <cmath>
#include <exception>
#include <expected>
#include <format>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <toast/assets/animation.hpp>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/curve.hpp>
#include <toast/assets/data.hpp>
#include <toast/assets/material.hpp>
#include <toast/assets/material_instance.hpp>
#include <toast/assets/mesh.hpp>
#include <toast/assets/prefab.hpp>
#include <toast/assets/script.hpp>
#include <toast/assets/shader.hpp>
#include <toast/assets/skeleton.hpp>
#include <toast/assets/texture.hpp>
#include <toast/log.hpp>
#include <toast/params/text.hpp>
#include <toast/reflect/reflect_node.hpp>
#include <toast/scripting/lua_value_codec.hpp>
#include <toast/scripting/script_runtime.hpp>
#include <toast/world/node.hpp>
#include <toast/world/tree_lock.hpp>
#include <vector>

namespace toast::console {
namespace {

using namespace toast::params;

constexpr std::string_view k_whitespace = " \t";
constexpr std::string_view k_usage = "usage: node <node>.<variable> [value]   or   node <node>.<function> [arg;arg;...]";

std::mutex g_queue_mutex;
std::vector<std::function<void()>> g_queue;

void enqueue(std::function<void()> work) {
	const std::lock_guard lock(g_queue_mutex);
	g_queue.push_back(std::move(work));
}

auto trim(std::string_view text) -> std::string_view {
	const size_t first = text.find_first_not_of(k_whitespace);
	if (first == std::string_view::npos) {
		return {};
	}
	return text.substr(first, text.find_last_not_of(k_whitespace) - first + 1);
}

auto typed(std::string_view name) -> std::string {
	std::string out {name};
	std::ranges::replace(out, ' ', '_');
	return out;
}

auto lowerCase(char c) -> char {
	return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

auto sameText(std::string_view a, std::string_view b) -> bool {
	return std::ranges::equal(a, b, [](char x, char y) { return lowerCase(x) == lowerCase(y); });
}

auto sameName(std::string_view node_name, std::string_view segment) -> bool {
	if (node_name.size() != segment.size()) {
		return false;
	}
	for (size_t i = 0; i < node_name.size(); ++i) {
		const char a = node_name[i] == ' ' ? '_' : node_name[i];
		if (lowerCase(a) != lowerCase(segment[i])) {
			return false;
		}
	}
	return true;
}

auto typeName(const Node& node) -> std::string {
	if (node.info() == nullptr) {
		return "Node";
	}
	std::string_view type = node.info()->type;
	if (type.starts_with("toast::")) {
		type.remove_prefix(7);
	}
	return std::string {type};
}

auto cleanType(std::string_view type) -> std::string {
	std::string out {type};
	for (const std::string_view junk : {"const ", "toast::", "std::", "glm::"}) {
		for (size_t at = out.find(junk); at != std::string::npos; at = out.find(junk)) {
			out.erase(at, junk.size());
		}
	}
	std::erase(out, '&');
	return std::string {trim(out)};
}

auto splitArguments(std::string_view text) -> std::vector<std::string_view> {
	std::vector<std::string_view> out;
	if (trim(text).empty()) {
		return out;
	}
	size_t begin = 0;
	bool quoted = false;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\' && quoted) {
			++i;
		} else if (text[i] == '"') {
			quoted = !quoted;
		} else if (text[i] == ';' && !quoted) {
			out.push_back(trim(text.substr(begin, i - begin)));
			begin = i + 1;
		}
	}
	out.push_back(trim(text.substr(begin)));
	return out;
}

auto countWords(std::string_view text) -> size_t {
	size_t words = 0;
	bool inside = false;
	for (const char c : text) {
		const bool space = c == ' ' || c == '\t';
		if (!space && !inside) {
			++words;
		}
		inside = !space;
	}
	return words;
}

struct Found {
	Box<Node> node;
	std::string error;
};

void collect(const Node& node, std::string_view name, std::vector<Box<Node>>& out) {
	if (sameName(node.name(), name)) {
		out.push_back(node.box());
	}
	for (const auto& child : node.children()) {
		collect(*child, name, out);
	}
}

auto resolve(const Box<Node>& root, std::string_view path) -> Found {
	if (!root.exists()) {
		return {{}, "there is no world root to look in"};
	}
	const TreeReadLock lock(*root);

	Box<Node> current;
	std::string walked;
	size_t start = 0;
	while (true) {
		const size_t slash = path.find('/', start);
		const std::string_view segment = path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
		if (segment.empty()) {
			return {{}, std::format("'{}' has an empty name in it", path)};
		}

		std::vector<Box<Node>> matches;
		if (start == 0 && sameText(segment, "global")) {
			if (slash == std::string_view::npos) {
				return {{}, "global/ is followed by the name of a node, wherever it is"};
			}
			start = slash + 1;
			const size_t next = path.find('/', start);
			const std::string_view name = path.substr(start, next == std::string_view::npos ? std::string_view::npos : next - start);
			if (name.empty()) {
				return {{}, std::format("'{}' has an empty name in it", path)};
			}
			collect(*root, name, matches);
			if (matches.empty()) {
				return {{}, std::format("no node called '{}' anywhere", name)};
			}
			if (matches.size() > 1) {
				return {{}, std::format("{} nodes are called '{}'; start from one of their parents", matches.size(), name)};
			}
			current = matches.front();
			walked = "global/" + std::string {name};
			if (next == std::string_view::npos) {
				return {current, {}};
			}
			start = next + 1;
			continue;
		}
		if (start == 0) {
			if (sameText(segment, "world")) {
				matches.push_back(root);
			} else {
				for (const auto& child : root->children()) {
					if (sameName(child->name(), segment)) {
						matches.push_back(child);
					}
				}
			}
		} else {
			for (const auto& child : current->children()) {
				if (sameName(child->name(), segment)) {
					matches.push_back(child);
				}
			}
		}

		if (matches.empty()) {
			if (start == 0) {
				return {{}, std::format("no node called '{}' on the root; global/{} looks everywhere", segment, segment)};
			}
			return {{}, std::format("'{}' has no child called '{}'", walked, segment)};
		}
		if (matches.size() > 1) {
			return {
			  {},
			  std::format("'{}' has {} children called '{}'", walked.empty() ? std::string {"world"} : walked, matches.size(), segment)
			};
		}
		current = matches.front();
		walked += walked.empty() ? std::string {segment} : "/" + std::string {segment};

		if (slash == std::string_view::npos) {
			return {current, {}};
		}
		start = slash + 1;
	}
}

auto scalarOf(const std::any& value) -> std::optional<std::pair<Scalar, Type>> {
	const auto integer = [](auto v) { return std::pair<Scalar, Type> {Scalar {static_cast<int64_t>(v)}, Type::integer}; };

	if (const auto* v = std::any_cast<bool>(&value)) {
		return std::pair<Scalar, Type> {Scalar {*v}, Type::boolean};
	}
	if (const auto* v = std::any_cast<int>(&value)) {
		return integer(*v);
	}
	if (const auto* v = std::any_cast<unsigned int>(&value)) {
		return integer(*v);
	}
	if (const auto* v = std::any_cast<int64_t>(&value)) {
		return integer(*v);
	}
	if (const auto* v = std::any_cast<uint64_t>(&value)) {
		return integer(*v);
	}
	if (const auto* v = std::any_cast<long>(&value)) {
		return integer(*v);
	}
	if (const auto* v = std::any_cast<unsigned long>(&value)) {
		return integer(*v);
	}
	if (const auto* v = std::any_cast<short>(&value)) {
		return integer(*v);
	}
	if (const auto* v = std::any_cast<unsigned short>(&value)) {
		return integer(*v);
	}
	if (const auto* v = std::any_cast<unsigned char>(&value)) {
		return integer(*v);
	}
	if (const auto* v = std::any_cast<float>(&value)) {
		return std::pair<Scalar, Type> {Scalar {static_cast<double>(*v)}, Type::floating};
	}
	if (const auto* v = std::any_cast<double>(&value)) {
		return std::pair<Scalar, Type> {Scalar {*v}, Type::floating};
	}
	if (const auto* v = std::any_cast<std::string>(&value)) {
		return std::pair<Scalar, Type> {Scalar {*v}, Type::string};
	}
	if (const auto* v = std::any_cast<glm::vec2>(&value)) {
		return std::pair<Scalar, Type> {Scalar {*v}, Type::vec2};
	}
	if (const auto* v = std::any_cast<glm::vec3>(&value)) {
		return std::pair<Scalar, Type> {Scalar {*v}, Type::vec3};
	}
	if (const auto* v = std::any_cast<glm::vec4>(&value)) {
		return std::pair<Scalar, Type> {Scalar {*v}, Type::vec4};
	}
	if (const auto* v = std::any_cast<glm::quat>(&value)) {
		return std::pair<Scalar, Type> {Scalar {*v}, Type::quat};
	}
	return std::nullopt;
}

auto anyText(const std::any& value) -> std::string {
	if (const auto* uid = std::any_cast<toast::UID>(&value)) {
		return uid->data() == 0 ? "none" : assets::AssetManager::getURI(*uid);
	}
	if (const auto scalar = scalarOf(value)) {
		return toText(Value {scalar->first}, scalar->second);
	}
	if (const auto* box = std::any_cast<Box<Node>>(&value)) {
		return box->exists() ? typed((*box)->name()) : "none";
	}
	return {};
}

auto fieldText(Node& node, const FieldInfo& field) -> std::string {
	if (field.is_array) {
		return "(list)";
	}
	if (field.get == nullptr) {
		return {};
	}
	return anyText(field.get(&node));
}

auto scalarFrom(std::string_view text, Type type) -> std::expected<Scalar, std::string> {
	auto value = fromText(text, type, false);
	if (!value) {
		return std::unexpected(value.error());
	}
	return std::get<Scalar>(*value);
}

template<typename Int>
auto clampedInteger(int64_t value) -> Int {
	const auto lowest = static_cast<int64_t>(std::numeric_limits<Int>::min());
	const int64_t highest = std::in_range<int64_t>(std::numeric_limits<Int>::max())
	                            ? static_cast<int64_t>(std::numeric_limits<Int>::max())
	                            : std::numeric_limits<int64_t>::max();
	return static_cast<Int>(std::clamp(value, lowest, highest));
}

auto quatFrom(std::string_view text) -> std::expected<glm::quat, std::string> {
	if (countWords(text) == 3) {
		auto angles = scalarFrom(text, Type::vec3);
		if (!angles) {
			return std::unexpected(angles.error());
		}
		return glm::quat {glm::radians(std::get<glm::vec3>(*angles))};
	}
	auto value = scalarFrom(text, Type::quat);
	if (!value) {
		return std::unexpected(value.error());
	}
	return std::get<glm::quat>(*value);
}

using Setter = std::function<void(Node&)>;

auto fieldSetter(const FieldInfo& field, std::string_view text) -> std::expected<Setter, std::string> {
	if (field.is_array) {
		return std::unexpected("it is a list, which cannot be typed here");
	}
	const std::string name {field.name};

	const auto make = [&](Type type, auto&& convert) -> std::expected<Setter, std::string> {
		auto scalar = scalarFrom(text, type);
		if (!scalar) {
			return std::unexpected(scalar.error());
		}
		return convert(*scalar);
	};

	switch (field.value_type) {
		case FieldType::bool_t:
			return make(Type::boolean, [&](const Scalar& s) -> Setter {
				return [name, v = std::get<bool>(s)](Node& n) { n.set<bool>(name, v); };
			});
		case FieldType::int_t:
			return make(Type::integer, [&](const Scalar& s) -> Setter {
				return [name, v = clampedInteger<int>(std::get<int64_t>(s))](Node& n) { n.set<int>(name, v); };
			});
		case FieldType::float_t:
			return make(Type::floating, [&](const Scalar& s) -> Setter {
				return [name, v = static_cast<float>(std::get<double>(s))](Node& n) { n.set<float>(name, v); };
			});
		case FieldType::double_t:
			return make(Type::floating, [&](const Scalar& s) -> Setter {
				return [name, v = std::get<double>(s)](Node& n) { n.set<double>(name, v); };
			});
		case FieldType::string_t:
			return make(Type::string, [&](const Scalar& s) -> Setter {
				return [name, v = std::get<std::string>(s)](Node& n) { n.set<std::string>(name, v); };
			});
		case FieldType::vec2_t:
			return make(Type::vec2, [&](const Scalar& s) -> Setter {
				return [name, v = std::get<glm::vec2>(s)](Node& n) { n.set<glm::vec2>(name, v); };
			});
		case FieldType::vec3_t:
			return make(Type::vec3, [&](const Scalar& s) -> Setter {
				return [name, v = std::get<glm::vec3>(s)](Node& n) { n.set<glm::vec3>(name, v); };
			});
		case FieldType::vec4_t:
			return make(Type::vec4, [&](const Scalar& s) -> Setter {
				return [name, v = std::get<glm::vec4>(s)](Node& n) { n.set<glm::vec4>(name, v); };
			});
		case FieldType::quaternion_t: {
			auto rotation = quatFrom(text);
			if (!rotation) {
				return std::unexpected(rotation.error());
			}
			return Setter {[name, v = *rotation](Node& n) { n.set<glm::quat>(name, v); }};
		}
		default: return std::unexpected("it cannot be typed here");
	}
}

struct Argument {
	std::any value;
	std::string text;
	bool view = false;
};

auto isWholeNumber(std::string_view text) -> bool {
	int64_t ignored = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), ignored);
	return error == std::errc {} && end == text.data() + text.size();
}

auto isNumber(std::string_view text) -> bool {
	double ignored = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), ignored);
	return error == std::errc {} && end == text.data() + text.size();
}

auto guessArgument(std::string_view text) -> std::any {
	if (text == "true" || text == "false") {
		return text == "true";
	}
	if (isWholeNumber(text)) {
		int64_t number = 0;
		std::from_chars(text.data(), text.data() + text.size(), number);
		return clampedInteger<int>(number);
	}
	if (isNumber(text)) {
		double number = 0;
		std::from_chars(text.data(), text.data() + text.size(), number);
		return number;
	}
	if (const size_t words = countWords(text); words >= 2 && words <= 4 && text.front() != '"') {
		Type type = Type::vec4;
		if (words == 2) {
			type = Type::vec2;
		} else if (words == 3) {
			type = Type::vec3;
		}
		const auto vector = scalarFrom(text, type);
		if (vector) {
			if (const auto* v = std::get_if<glm::vec2>(&*vector)) {
				return *v;
			}
			if (const auto* v = std::get_if<glm::vec3>(&*vector)) {
				return *v;
			}
			return std::get<glm::vec4>(*vector);
		}
	}
	if (auto string = scalarFrom(text, Type::string)) {
		return std::get<std::string>(*string);
	}
	return std::string {text};
}

struct HandleKind {
	const std::type_info* id;
	std::string_view asset_type;
	std::function<std::any(assets::Asset*, toast::UID, std::string_view)> make;
};

template<typename T>
auto handleKind(std::string_view asset_type) -> HandleKind {
	return {&typeid(assets::Handle<T>), asset_type, [](assets::Asset* asset, toast::UID uid, std::string_view uri) -> std::any {
		        return assets::Handle<T>(asset, uid, uri);
	        }};
}

auto handleKinds() -> const std::vector<HandleKind>& {
	static const std::vector<HandleKind> kinds {
	  handleKind<assets::Mesh>("mesh"),
	  handleKind<assets::Texture>("texture"),
	  handleKind<assets::Material>("material"),
	  handleKind<assets::MaterialInstance>("material_instance"),
	  handleKind<assets::Animation>("animation"),
	  handleKind<assets::Skeleton>("skeleton"),
	  handleKind<assets::Prefab>("node"),
	  handleKind<assets::Curve>("curve"),
	  handleKind<assets::Data>("data"),
	  handleKind<assets::Script>("script"),
	  handleKind<assets::Shader>("shader"),
	};
	return kinds;
}

auto handleKindOf(const std::type_info* id) -> const HandleKind* {
	if (id == nullptr) {
		return nullptr;
	}
	const auto at = std::ranges::find_if(handleKinds(), [&](const HandleKind& kind) { return *kind.id == *id; });
	return at == handleKinds().end() ? nullptr : &*at;
}

auto assetTypeOfSpelling(std::string_view spelling) -> std::string {
	const size_t open = spelling.find("Handle<");
	const size_t close = spelling.rfind('>');
	if (open == std::string_view::npos || close == std::string_view::npos || close <= open) {
		return {};
	}
	std::string_view name = spelling.substr(open + 7, close - open - 7);
	if (const size_t colon = name.rfind(':'); colon != std::string_view::npos) {
		name = name.substr(colon + 1);
	}
	if (name == "Prefab") {
		return "node";
	}
	std::string out;
	for (const char c : name) {
		if (std::isupper(static_cast<unsigned char>(c)) != 0 && !out.empty()) {
			out += '_';
		}
		out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return out;
}

auto pathText(std::string_view text) -> std::expected<std::string, std::string> {
	text = trim(text);
	if (text.starts_with('"')) {
		auto quoted = scalarFrom(text, Type::string);
		if (!quoted) {
			return std::unexpected(quoted.error());
		}
		return std::get<std::string>(*quoted);
	}
	return std::string {text};
}

auto assetByPath(std::string_view text, std::string_view asset_type) -> std::expected<toast::UID, std::string> {
	const auto path = pathText(text);
	if (!path) {
		return std::unexpected(path.error());
	}
	if (path->empty() || *path == "none") {
		return toast::UID {};
	}
	if (!assets::AssetManager::isInitialized()) {
		return std::unexpected("there are no assets to look in");
	}
	const auto uid = assets::AssetManager::resolveURI(*path);
	if (!uid) {
		return std::unexpected(std::format("no asset '{}'", *path));
	}
	if (const std::string actual = assets::AssetManager::typeOf(*uid);
	    !asset_type.empty() && !actual.empty() && actual != asset_type) {
		return std::unexpected(std::format("'{}' is a {}, and this takes a {}", *path, actual, asset_type));
	}
	return *uid;
}

auto parseArgument(const ParameterInfo& parameter, std::string_view text, const NodeRoot& root)
    -> std::expected<Argument, std::string> {
	const std::type_info& type = parameter.type_id != nullptr ? *parameter.type_id : typeid(void);
	Argument argument;
	argument.text = std::string {text};

	const auto fail = [&](const std::string& message) -> std::expected<Argument, std::string> {
		return std::unexpected(std::format("{}: {}", parameter.name, message));
	};

	if (type == typeid(bool)) {
		auto v = scalarFrom(text, Type::boolean);
		if (!v) {
			return fail(v.error());
		}
		argument.value = std::get<bool>(*v);
		return argument;
	}
	if (type == typeid(int) || type == typeid(unsigned int) || type == typeid(int64_t) || type == typeid(uint64_t) ||
	    type == typeid(uint8_t) || type == typeid(int16_t) || type == typeid(uint16_t)) {
		auto v = scalarFrom(text, Type::integer);
		if (!v) {
			return fail(v.error());
		}
		const int64_t n = std::get<int64_t>(*v);
		if (type == typeid(int)) {
			argument.value = clampedInteger<int>(n);
		} else if (type == typeid(unsigned int)) {
			argument.value = clampedInteger<unsigned int>(n);
		} else if (type == typeid(int64_t)) {
			argument.value = n;
		} else if (type == typeid(uint64_t)) {
			argument.value = static_cast<uint64_t>(std::max<int64_t>(n, 0));
		} else if (type == typeid(uint8_t)) {
			argument.value = clampedInteger<uint8_t>(n);
		} else if (type == typeid(int16_t)) {
			argument.value = clampedInteger<int16_t>(n);
		} else {
			argument.value = clampedInteger<uint16_t>(n);
		}
		return argument;
	}
	if (type == typeid(float) || type == typeid(double)) {
		auto v = scalarFrom(text, Type::floating);
		if (!v) {
			return fail(v.error());
		}
		if (type == typeid(float)) {
			argument.value = static_cast<float>(std::get<double>(*v));
		} else {
			argument.value = std::get<double>(*v);
		}
		return argument;
	}
	if (type == typeid(std::string) || type == typeid(std::string_view)) {
		auto v = scalarFrom(text, Type::string);
		if (!v) {
			return fail(v.error());
		}
		argument.text = std::get<std::string>(*v);
		argument.view = type == typeid(std::string_view);
		argument.value = argument.text;
		return argument;
	}
	if (type == typeid(glm::vec2) || type == typeid(glm::vec3) || type == typeid(glm::vec4)) {
		Type wanted = Type::vec4;
		if (type == typeid(glm::vec2)) {
			wanted = Type::vec2;
		} else if (type == typeid(glm::vec3)) {
			wanted = Type::vec3;
		}
		auto v = scalarFrom(text, wanted);
		if (!v) {
			return fail(v.error());
		}
		if (const auto* a = std::get_if<glm::vec2>(&*v)) {
			argument.value = *a;
		} else if (const auto* b = std::get_if<glm::vec3>(&*v)) {
			argument.value = *b;
		} else {
			argument.value = std::get<glm::vec4>(*v);
		}
		return argument;
	}
	if (type == typeid(glm::quat)) {
		auto v = quatFrom(text);
		if (!v) {
			return fail(v.error());
		}
		argument.value = *v;
		return argument;
	}
	if (const HandleKind* kind = handleKindOf(parameter.type_id)) {
		const auto uid = assetByPath(text, kind->asset_type);
		if (!uid) {
			return fail(uid.error());
		}
		assets::Asset* asset = uid->data() != 0 ? assets::AssetManager::get().load(*uid) : nullptr;
		argument.value = kind->make(asset, *uid, uid->data() != 0 ? assets::AssetManager::getURI(*uid) : std::string {});
		return argument;
	}
	if (type == typeid(Box<Node>)) {
		auto found = resolve(root ? root() : Box<Node> {}, text);
		if (!found.node.exists()) {
			return fail(found.error);
		}
		argument.value = found.node;
		return argument;
	}
	return fail(std::format("a {} cannot be typed here", cleanType(parameter.type)));
}

auto parametersText(const FunctionInfo& function) -> std::string {
	std::string out;
	for (const auto& parameter : function.parameters) {
		out += out.empty() ? "" : "; ";
		out += std::format("{} {}", cleanType(parameter.type), parameter.name);
		if (parameter.default_value) {
			out += std::format(" = {}", *parameter.default_value);
		}
	}
	return out;
}

enum class MemberKind : uint8_t {
	field,
	enabled,
	function,
	lua_variable,
	lua_function
};

struct Member {
	MemberKind kind = MemberKind::field;
	std::string name;
	std::string detail;
	const FieldInfo* field = nullptr;
	const FunctionInfo* function = nullptr;
	std::optional<scripting::LuaVarDesc> variable;
};

auto luaKindName(scripting::LuaVarKind kind) -> std::string {
	using scripting::LuaVarKind;
	switch (kind) {
		case LuaVarKind::boolean: return "bool";
		case LuaVarKind::integer: return "int";
		case LuaVarKind::number: return "number";
		case LuaVarKind::string: return "string";
		case LuaVarKind::vec2: return "vec2";
		case LuaVarKind::vec3: return "vec3";
		case LuaVarKind::vec4: return "vec4";
		case LuaVarKind::color3: return "color3";
		case LuaVarKind::color4: return "color4";
		case LuaVarKind::node_ref: return "node";
		case LuaVarKind::asset_ref: return "asset";
	}
	return {};
}

auto findMember(std::vector<Member>& all, std::string_view name) -> std::optional<Member> {
	for (auto& candidate : all) {
		if (candidate.name == name) {
			return candidate;
		}
	}
	const Member* found = nullptr;
	for (const auto& candidate : all) {
		if (sameText(candidate.name, name)) {
			if (found != nullptr) {
				return std::nullopt;
			}
			found = &candidate;
		}
	}
	return found != nullptr ? std::optional<Member> {*found} : std::nullopt;
}

auto membersOf(Node& node) -> std::vector<Member> {
	std::vector<Member> out;
	std::set<std::string, std::less<>> seen;

	for (const NodeInfo* info = node.info(); info != nullptr; info = info->base_type) {
		for (const auto& field : info->all_fields) {
			if (field.hasAttribute("Hidden") || !seen.insert(std::string {field.name}).second) {
				continue;
			}
			Member member;
			member.kind = MemberKind::field;
			member.name = std::string {field.name};
			member.detail = fieldText(node, field);
			member.field = &field;
			out.push_back(std::move(member));
		}
	}
	for (const NodeInfo* info = node.info(); info != nullptr; info = info->base_type) {
		for (const auto& function : info->methods) {
			if (!seen.insert(std::string {function.name}).second) {
				continue;
			}
			Member member;
			member.kind = MemberKind::function;
			member.name = std::string {function.name};
			member.detail = parametersText(function);
			member.function = &function;
			out.push_back(std::move(member));
		}
	}

	if (!seen.contains("enabled")) {
		Member member;
		member.kind = MemberKind::enabled;
		member.name = "enabled";
		member.detail = node.enabled() ? "true" : "false";
		out.push_back(std::move(member));
		seen.insert("enabled");
	}

	if (auto* runtime = node.scriptRuntime()) {
		for (size_t i = 0; i < runtime->instanceCount(); ++i) {
			if (const auto* schema = runtime->instanceSchema(i)) {
				schema->forEach([&](const scripting::LuaVarDesc& variable) {
					if (variable.hidden || !seen.insert(variable.name).second) {
						return;
					}
					Member member;
					member.kind = MemberKind::lua_variable;
					member.name = variable.name;
					member.detail = luaKindName(variable.kind);
					member.variable = variable;
					out.push_back(std::move(member));
				});
			}
		}
		for (const auto& function : runtime->functions()) {
			if (function.name.starts_with('_') || !seen.insert(function.name).second) {
				continue;
			}
			Member member;
			member.kind = MemberKind::lua_function;
			member.name = function.name;
			for (const auto& parameter : function.parameters) {
				member.detail += member.detail.empty() ? parameter : "; " + parameter;
			}
			out.push_back(std::move(member));
		}
	}
	return out;
}

auto entryKindOf(MemberKind kind) -> EntryKind {
	return kind == MemberKind::function || kind == MemberKind::lua_function ? EntryKind::function : EntryKind::field;
}

auto handleSetter(const FieldInfo& field, std::string_view text) -> std::optional<std::expected<Setter, std::string>> {
	if (field.value_type != FieldType::uid_t || field.is_array || field.isNodeReference()) {
		return std::nullopt;
	}
	const std::string asset_type = assetTypeOfSpelling(field.type);
	if (asset_type.empty()) {
		return std::nullopt;
	}
	const auto uid = assetByPath(text, asset_type);
	if (!uid) {
		return std::unexpected(uid.error());
	}
	return Setter {[name = std::string {field.name}, uid = *uid](Node& n) { n.set<toast::UID>(name, uid); }};
}

auto bodyMover(const Node& node, const FieldInfo& field, std::string_view text) -> std::optional<Setter> {
	if (node.info() == nullptr || field.is_array) {
		return std::nullopt;
	}
	std::vector<std::string_view> names;
	if (field.name == "position" && field.value_type == FieldType::vec3_t) {
		names = {"teleport", "setPosition"};
	} else if (field.name == "rotation" && field.value_type == FieldType::quaternion_t) {
		names = {"setRotation"};
	}
	for (const std::string_view name : names) {
		const FunctionInfo* method = node.info()->getMethod(name);
		if (method == nullptr || method->parameters.size() != 1) {
			continue;
		}
		std::any value;
		if (field.value_type == FieldType::vec3_t) {
			const auto vector = scalarFrom(text, Type::vec3);
			if (!vector) {
				return std::nullopt;
			}
			value = std::get<glm::vec3>(*vector);
		} else {
			const auto rotation = quatFrom(text);
			if (!rotation) {
				return std::nullopt;
			}
			value = *rotation;
		}
		return Setter {[name = std::string {name}, value = std::move(value)](Node& target) {
			std::vector<std::any> arguments {value};
			try {
				(void)target.info()->callAllDynamic(&target, name, arguments);
			} catch (const std::exception& e) { TOAST_WARN("Console", "{} failed: {}", name, e.what()); }
		}};
	}
	return std::nullopt;
}

auto typeOf(FieldType type) -> std::optional<Type> {
	switch (type) {
		case FieldType::bool_t: return Type::boolean;
		case FieldType::int_t: return Type::integer;
		case FieldType::float_t:
		case FieldType::double_t: return Type::floating;
		case FieldType::string_t: return Type::string;
		case FieldType::vec2_t: return Type::vec2;
		case FieldType::vec3_t: return Type::vec3;
		case FieldType::vec4_t: return Type::vec4;
		case FieldType::quaternion_t: return Type::quat;
		default: return std::nullopt;
	}
}

auto typeOf(scripting::LuaVarKind kind) -> std::optional<Type> {
	using scripting::LuaVarKind;
	switch (kind) {
		case LuaVarKind::boolean: return Type::boolean;
		case LuaVarKind::integer: return Type::integer;
		case LuaVarKind::number: return Type::floating;
		case LuaVarKind::string: return Type::string;
		case LuaVarKind::vec2: return Type::vec2;
		case LuaVarKind::vec3:
		case LuaVarKind::color3: return Type::vec3;
		case LuaVarKind::vec4:
		case LuaVarKind::color4: return Type::vec4;
		default: return std::nullopt;
	}
}

}

auto executeNode(const NodeRoot& root_of, std::string_view arguments) -> Result {
	arguments = trim(arguments);
	if (arguments.empty()) {
		return {false, std::string {k_usage}};
	}
	const size_t spec_end = arguments.find_first_of(k_whitespace);
	const std::string_view spec = arguments.substr(0, spec_end);
	const std::string_view rest = spec_end == std::string_view::npos ? std::string_view {} : trim(arguments.substr(spec_end));

	const size_t slash = spec.rfind('/');
	const size_t dot = spec.rfind('.');
	const bool has_member = dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash);
	const std::string_view path = has_member ? spec.substr(0, dot) : spec;
	const std::string_view member_name = has_member ? spec.substr(dot + 1) : std::string_view {};

	Box<Node> root = root_of ? root_of() : Box<Node> {};
	Found found = resolve(root, path);
	if (!found.node.exists()) {
		return {false, found.error};
	}
	Node& node = *found.node;

	if (!has_member) {
		return {true, std::format("{}: {}, {} children", path, typeName(node), node.children().size())};
	}
	if (member_name.empty()) {
		return {false, std::format("{}: say what to do with it, after the dot", path)};
	}

	std::vector<Member> all;
	{
		const TreeReadLock lock(node);
		all = membersOf(node);
	}
	std::optional<Member> member = findMember(all, member_name);
	if (!member) {
		std::vector<std::pair<int, std::string>> close;
		for (const auto& candidate : all) {
			if (const auto score = fuzzyScore(member_name, candidate.name)) {
				close.emplace_back(*score, candidate.name);
			}
		}
		std::ranges::sort(close, [](const auto& a, const auto& b) { return a.first > b.first; });
		std::string message = std::format("{} ({}) has no variable or function '{}'", path, typeName(node), member_name);
		for (size_t i = 0; i < close.size() && i < 3; ++i) {
			message += i == 0 ? ", did you mean " : (i + 1 == close.size() || i == 2 ? " or " : ", ");
			message += close[i].second;
		}
		message += close.empty() ? "" : "?";
		return {false, std::move(message)};
	}

	const std::string label = std::format("{}.{}", path, member->name);

	switch (member->kind) {
		case MemberKind::enabled: {
			if (rest.empty()) {
				return {true, std::format("{} = {}", label, member->detail)};
			}
			std::string text {rest};
			if (hasRelative(rest)) {
				auto worked_out = resolveRelative(rest, member->detail, Type::boolean);
				if (!worked_out) {
					return {false, std::format("{}: {}", label, worked_out.error())};
				}
				text = std::move(*worked_out);
			}
			const auto value = scalarFrom(text, Type::boolean);
			if (!value) {
				return {false, std::format("{}: {}", label, value.error())};
			}
			const bool on = std::get<bool>(*value);
			enqueue([box = found.node, on]() mutable {
				if (box.exists()) {
					box->enabled(on);
				}
			});
			return {true, std::format("{} = {}", label, on ? "true" : "false")};
		}

		case MemberKind::field: {
			if (rest.empty()) {
				return {true, std::format("{} = {}", label, member->detail)};
			}
			// `~` is what it is now
			std::string text {rest};
			if (hasRelative(rest)) {
				const auto type = typeOf(member->field->value_type);
				if (member->field->is_array || !type) {
					return {false, std::format("{}: it cannot be written relative to itself", label)};
				}
				std::string current;
				{
					const TreeReadLock lock(node);
					current = fieldText(node, *member->field);
				}
				auto worked_out = resolveRelative(rest, current, *type);
				if (!worked_out) {
					return {false, std::format("{}: {}", label, worked_out.error())};
				}
				text = std::move(*worked_out);
			}
			auto setter = [&]() -> std::expected<Setter, std::string> {
				if (auto handle = handleSetter(*member->field, text)) {
					return std::move(*handle);
				}
				if (auto mover = bodyMover(node, *member->field, text)) {
					return std::move(*mover);
				}
				return fieldSetter(*member->field, text);
			}();
			if (!setter) {
				return {false, std::format("{}: {}", label, setter.error())};
			}
			enqueue([box = found.node, setter = std::move(*setter)]() mutable {
				if (box.exists()) {
					setter(*box);
				}
			});
			return {true, std::format("{} = {}", label, text)};
		}

		case MemberKind::lua_variable: {
			if (rest.empty()) {
				return {true, std::format("{} is a {} of the scripts; give it a value to set it", label, member->detail)};
			}
			std::string text {rest};
			if (hasRelative(rest)) {
				const auto type = typeOf(member->variable->kind);
				auto* runtime = node.scriptRuntime();
				if (member->variable->is_array || !type || runtime == nullptr) {
					return {false, std::format("{}: it cannot be written relative to itself", label)};
				}
				const std::string current = scripting::stringifyLuaValue(*member->variable, runtime->getVar(member->name));
				auto worked_out = resolveRelative(rest, current, *type);
				if (!worked_out) {
					return {false, std::format("{}: {}", label, worked_out.error())};
				}
				text = std::move(*worked_out);
			}
			if (member->variable->kind == scripting::LuaVarKind::asset_ref && text.find("://") != std::string::npos) {
				const auto uid = assetByPath(text, member->variable->ref_type);
				if (!uid) {
					return {false, std::format("{}: {}", label, uid.error())};
				}
				text = uid->get();
			}
			std::any value;
			try {
				value = scripting::parseLuaValue(*member->variable, text, [](std::string_view) { return Box<Node> {}; });
			} catch (const std::exception&) { return {false, std::format("{}: that is not a {}", label, member->detail)}; }
			if (!value.has_value()) {
				return {false, std::format("{}: that is not a {}", label, member->detail)};
			}
			enqueue([box = found.node, name = member->name, value = std::move(value)]() mutable {
				if (box.exists()) {
					if (auto* runtime = box->scriptRuntime()) {
						runtime->setVar(name, value);
					}
				}
			});
			return {true, std::format("{} = {}", label, text)};
		}

		case MemberKind::function: {
			const FunctionInfo& function = *member->function;
			const auto texts = splitArguments(rest);
			size_t required = 0;
			for (const auto& parameter : function.parameters) {
				required += parameter.default_value ? 0 : 1;
			}
			if (texts.size() < required || texts.size() > function.parameters.size()) {
				return {
				  false,
				  std::format("{} takes ({}), separated by ;", label, member->detail.empty() ? std::string {"nothing"} : member->detail)
				};
			}

			std::vector<Argument> values(function.parameters.size());
			for (size_t i = 0; i < texts.size(); ++i) {
				auto argument = parseArgument(function.parameters[i], texts[i], root_of);
				if (!argument) {
					return {false, std::format("{}: {}", label, argument.error())};
				}
				values[i] = std::move(*argument);
			}
			enqueue([box = found.node, name = member->name, values = std::move(values)]() mutable {
				if (!box.exists()) {
					return;
				}
				std::vector<std::any> arguments;
				arguments.reserve(values.size());
				for (const auto& value : values) {
					arguments.push_back(value.view ? std::any {std::string_view {value.text}} : value.value);
				}
				try {
					if (box->info() != nullptr && box->info()->getMethod(name)) {
						(void)box->info()->callAllDynamic(&*box, name, arguments);
					}
					_detail::callNodeScripts(&*box, name, arguments);
				} catch (const std::exception& e) { TOAST_WARN("Console", "{} failed: {}", name, e.what()); }
			});
			return {true, std::format("called {}({})", label, rest)};
		}

		case MemberKind::lua_function: {
			std::vector<std::any> values;
			for (const auto text : splitArguments(rest)) {
				if (text.empty()) {
					return {false, std::format("{}: an argument is empty", label)};
				}
				values.push_back(guessArgument(text));
			}
			enqueue([box = found.node, name = member->name, values = std::move(values)]() mutable {
				if (box.exists()) {
					_detail::callNodeScripts(&*box, name, values);
				}
			});
			return {true, std::format("called {}({})", label, rest)};
		}
	}
	return {false, std::string {k_usage}};
}

auto describeNode(const NodeRoot& root_of, std::string_view spec) -> Result {
	spec = trim(spec);
	const size_t slash = spec.rfind('/');
	const size_t dot = spec.rfind('.');
	if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
		return {false, "name a variable of a node: <node>.<variable>"};
	}
	const std::string_view path = spec.substr(0, dot);
	const std::string_view member_name = spec.substr(dot + 1);

	Found found = resolve(root_of ? root_of() : Box<Node> {}, path);
	if (!found.node.exists()) {
		return {false, found.error};
	}
	Node& node = *found.node;
	std::vector<Member> all;
	{
		const TreeReadLock lock(node);
		all = membersOf(node);
	}
	const auto member = findMember(all, member_name);
	if (!member) {
		return {false, std::format("{} ({}) has no variable '{}'", path, typeName(node), member_name)};
	}

	switch (member->kind) {
		case MemberKind::field:
		case MemberKind::enabled: return {true, std::format("{}.{} = {}", path, member->name, member->detail)};
		case MemberKind::lua_variable: {
			std::string value = member->detail;
			if (auto* runtime = node.scriptRuntime()) {
				value = scripting::stringifyLuaValue(*member->variable, runtime->getVar(member->name));
			}
			return {true, std::format("{}.{} = {}", path, member->name, value)};
		}
		default: return {false, std::format("{}.{} is a function; only a variable can be pinned", path, member->name)};
	}
}

void runQueuedNodeWork() {
	std::vector<std::function<void()>> work;
	{
		const std::lock_guard lock(g_queue_mutex);
		work.swap(g_queue);
	}
	for (auto& item : work) {
		item();
	}
}

auto completePath(std::string_view written, size_t begin, std::string_view asset_type) -> Completion {
	Completion completion;
	if (!assets::AssetManager::isInitialized() || asset_type.empty()) {
		return completion;
	}
	const bool quoted = written.starts_with('"');
	if (quoted) {
		written.remove_prefix(1);
		++begin;
	}

	std::vector<std::string> uris;
	for (const toast::UID uid : assets::AssetManager::get().listByType(asset_type)) {
		if (auto uri = assets::AssetManager::getURI(uid); !uri.empty()) {
			uris.push_back(std::move(uri));
		}
	}
	std::ranges::sort(uris);

	const size_t scheme_end = written.find("://");
	std::vector<Scored> scored;
	std::set<std::string> offered;

	if (scheme_end == std::string_view::npos) {
		completion.replace_begin = begin;
		completion.replace_end = begin + written.size();
		for (const auto& uri : uris) {
			const std::string scheme = uri.substr(0, uri.find("://")) + "://";
			if (const auto score = fuzzyScore(written, scheme); score && offered.insert(scheme).second) {
				scored.push_back({
				  {scheme, scheme, {}, EntryKind::node},
          *score
				});
			}
		}
	} else {
		const size_t folder_end = written.rfind('/');
		const std::string_view folder = written.substr(0, folder_end + 1);
		const std::string_view partial = written.substr(folder_end + 1);
		completion.replace_begin = begin + folder.size();
		completion.replace_end = begin + written.size();
		for (const auto& uri : uris) {
			if (!uri.starts_with(folder)) {
				continue;
			}
			const std::string_view rest = std::string_view {uri}.substr(folder.size());
			const size_t slash = rest.find('/');
			const bool is_folder = slash != std::string_view::npos;
			const std::string name {rest.substr(0, is_folder ? slash : rest.size())};
			const std::string shown = is_folder ? name + "/" : name;
			if (const auto score = fuzzyScore(partial, name); score && offered.insert(shown).second) {
				scored.push_back({
				  {shown, shown, is_folder ? std::string {} : std::string {asset_type}, is_folder ? EntryKind::node : EntryKind::value},
				  *score
				});
			}
		}
	}
	if (!written.empty()) {
		rank(scored);
	}
	for (auto& item : scored) {
		completion.entries.push_back(std::move(item.entry));
	}
	return completion;
}

auto completeNode(const NodeRoot& root_of, std::string_view line, size_t cursor, size_t base) -> Completion {
	Completion completion;
	Box<Node> root = root_of ? root_of() : Box<Node> {};
	if (!root.exists()) {
		return completion;
	}

	const size_t spec_begin = std::min(line.find_first_not_of(k_whitespace, std::min(base, line.size())), line.size());
	size_t spec_end = line.find_first_of(k_whitespace, spec_begin);
	if (spec_end == std::string_view::npos) {
		spec_end = line.size();
	}
	cursor = std::clamp(cursor, spec_begin, line.size());

	if (cursor > spec_end) {
		const std::string_view spec = line.substr(spec_begin, spec_end - spec_begin);
		const size_t spec_slash = spec.rfind('/');
		const size_t spec_dot = spec.rfind('.');
		if (spec_dot == std::string_view::npos || (spec_slash != std::string_view::npos && spec_dot < spec_slash)) {
			return completion;
		}
		Found found = resolve(root, spec.substr(0, spec_dot));
		if (!found.node.exists()) {
			return completion;
		}
		std::vector<Member> members;
		{
			const TreeReadLock lock(*found.node);
			members = membersOf(*found.node);
		}
		auto found_member = findMember(members, spec.substr(spec_dot + 1));
		if (!found_member) {
			return completion;
		}
		const auto member = &*found_member;

		completion.hint = member->detail;
		if (member->kind == MemberKind::lua_variable) {
			if (auto* runtime = found.node->scriptRuntime()) {
				completion.hint = scripting::stringifyLuaValue(*member->variable, runtime->getVar(member->name));
			}
		}

		const size_t arguments_begin = std::min(line.find_first_not_of(k_whitespace, spec_end), line.size());
		const size_t from = std::min(arguments_begin, cursor);
		const std::string_view written = line.substr(from, cursor - from);

		size_t piece_begin = from;
		size_t index = 0;
		if (member->kind == MemberKind::function || member->kind == MemberKind::lua_function) {
			bool quoted = false;
			for (size_t i = 0; i < written.size(); ++i) {
				if (written[i] == '\\' && quoted) {
					++i;
				} else if (written[i] == '"') {
					quoted = !quoted;
				} else if (written[i] == ';' && !quoted) {
					piece_begin = from + i + 1;
					++index;
				}
			}
		}
		const size_t piece_start = std::min(line.find_first_not_of(k_whitespace, piece_begin), cursor);
		const std::string_view partial = line.substr(piece_start, cursor - piece_start);
		completion.replace_begin = piece_start;
		completion.replace_end = cursor;

		std::string asset_type;
		if (member->kind == MemberKind::field && member->field->value_type == FieldType::uid_t && !member->field->is_array &&
		    !member->field->isNodeReference()) {
			asset_type = assetTypeOfSpelling(member->field->type);
		} else if (member->kind == MemberKind::function && index < member->function->parameters.size()) {
			if (const HandleKind* kind = handleKindOf(member->function->parameters[index].type_id)) {
				asset_type = kind->asset_type;
			}
		} else if (member->kind == MemberKind::lua_variable && member->variable->kind == scripting::LuaVarKind::asset_ref) {
			asset_type = member->variable->ref_type;
		}
		if (!asset_type.empty()) {
			Completion paths = completePath(partial, piece_start, asset_type);
			paths.hint = completion.hint;
			return paths;
		}

		bool boolean = member->kind == MemberKind::enabled;
		if (member->kind == MemberKind::field) {
			boolean = member->field->value_type == FieldType::bool_t && !member->field->is_array;
		} else if (member->kind == MemberKind::function && index < member->function->parameters.size()) {
			const auto* id = member->function->parameters[index].type_id;
			boolean = id != nullptr && *id == typeid(bool);
		}
		if (boolean) {
			std::vector<Scored> names;
			for (const std::string_view name : {"true", "false"}) {
				if (const auto score = fuzzyScore(partial, name)) {
					names.push_back({
					  {std::string {name}, std::string {name}, {}, EntryKind::value},
            *score
					});
				}
			}
			if (!partial.empty()) {
				rank(names);
			}
			for (auto& item : names) {
				completion.entries.push_back(std::move(item.entry));
			}
		}
		return completion;
	}

	const std::string_view written = line.substr(spec_begin, cursor - spec_begin);
	const size_t slash = written.rfind('/');
	const size_t dot = written.rfind('.');

	if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash)) {
		Found found = resolve(root, written.substr(0, dot));
		if (!found.node.exists()) {
			return completion;
		}
		const std::string_view partial = written.substr(dot + 1);
		completion.replace_begin = spec_begin + dot + 1;
		completion.replace_end = spec_end;

		std::vector<Member> members;
		{
			const TreeReadLock lock(*found.node);
			members = membersOf(*found.node);
		}
		std::vector<Scored> fields;
		std::vector<Scored> functions;
		for (auto& member : members) {
			if (const auto score = fuzzyScore(partial, member.name)) {
				const EntryKind kind = entryKindOf(member.kind);
				(kind == EntryKind::field ? fields : functions)
				    .push_back({
				      {member.name, member.name, std::move(member.detail), kind},
              *score
				});
			}
		}
		append(completion.entries, fields);
		append(completion.entries, functions);
		return completion;
	}

	const std::string_view partial = slash == std::string_view::npos ? written : written.substr(slash + 1);
	completion.replace_begin = spec_begin + (slash == std::string_view::npos ? 0 : slash + 1);
	completion.replace_end = std::min(spec_end, std::max(cursor, line.find_first_of("/.", cursor)));

	struct Seen {
		std::string type;
		size_t count = 0;
	};

	std::vector<std::pair<std::string, Seen>> names;
	const auto note = [&](const Node& node) {
		const std::string name = typed(node.name());
		const auto at = std::ranges::find(names, name, &std::pair<std::string, Seen>::first);
		if (at == names.end()) {
			names.push_back({
			  name, {typeName(node), 1}
			});
		} else {
			++at->second.count;
		}
	};

	if (slash == std::string_view::npos) {
		const TreeReadLock lock(*root);
		names.push_back({
		  "world", {typeName(*root), 1}
		});
		names.push_back({
		  "global", {"anywhere", 1}
		});
		for (const auto& child : root->children()) {
			note(*child);
		}
	} else if (written.substr(0, slash) == "global") {
		const TreeReadLock lock(*root);
		const auto walk = [&](this auto&& self, const Node& node) -> void {
			note(node);
			for (const auto& child : node.children()) {
				self(*child);
			}
		};
		walk(*root);
	} else {
		Found scope = resolve(root, written.substr(0, slash));
		if (!scope.node.exists()) {
			return completion;
		}
		const TreeReadLock lock(*scope.node);
		for (const auto& child : scope.node->children()) {
			note(*child);
		}
	}

	std::vector<Scored> scored;
	for (auto& [name, seen] : names) {
		if (name.empty()) {
			continue;
		}
		if (const auto score = fuzzyScore(partial, name)) {
			std::string type = seen.type;
			if (seen.count > 1 && name != "world" && name != "global") {
				type += std::format(" x{}", seen.count);
			}
			scored.push_back({
			  {name, name, std::move(type), EntryKind::node},
        *score
			});
		}
	}
	append(completion.entries, scored);
	return completion;
}

}
