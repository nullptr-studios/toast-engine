#include "lua_value_codec.hpp"

#include "asset_proxy.hpp"
#include "lua_types.hpp"

#include <charconv>
#include <glm/glm.hpp>
#include <sstream>
#include <toast/assets/assets.hpp>
#include <toast/uid.hpp>
#include <toast/world/node.hpp>
#include <vector>

namespace scripting {

namespace {

auto parseFloats(std::string_view text) -> std::vector<float> {
	std::vector<float> out;
	std::istringstream ss {std::string(text)};
	float f = 0.0f;
	while (ss >> f) {
		out.push_back(f);
	}
	return out;
}

}

auto stringifyLuaValue(const LuaVarDesc& desc, const std::any& value) -> std::string {
	if (!value.has_value()) {
		return "";
	}

	if (desc.is_array) {
		auto join = [](const auto& vec, auto&& to_text, char sep = ' ') {
			std::string out;
			for (size_t i = 0; i < vec.size(); ++i) {
				if (i > 0) {
					out += sep;
				}
				out += to_text(vec[i]);
			}
			return out;
		};
		if (const auto* v = std::any_cast<std::vector<bool>>(&value)) {
			std::string out;
			for (size_t i = 0; i < v->size(); ++i) {
				out += (i > 0 ? " " : "");
				out += (*v)[i] ? "true" : "false";
			}
			return out;
		}
		if (const auto* v = std::any_cast<std::vector<int>>(&value)) {
			return join(*v, [](int e) { return std::format("{}", e); });
		}
		if (const auto* v = std::any_cast<std::vector<double>>(&value)) {
			return join(*v, [](double e) { return std::format("{}", e); });
		}
		if (const auto* v = std::any_cast<std::vector<std::string>>(&value)) {
			return join(*v, [](const std::string& e) { return e; }, '\x1f');
		}
		if (const auto* v = std::any_cast<std::vector<glm::vec2>>(&value)) {
			return join(*v, [](const glm::vec2& e) { return std::format("{} {}", e.x, e.y); });
		}
		if (const auto* v = std::any_cast<std::vector<glm::vec3>>(&value)) {
			return join(*v, [](const glm::vec3& e) { return std::format("{} {} {}", e.x, e.y, e.z); });
		}
		if (const auto* v = std::any_cast<std::vector<glm::vec4>>(&value)) {
			return join(*v, [](const glm::vec4& e) { return std::format("{} {} {} {}", e.x, e.y, e.z, e.w); });
		}
		if (const auto* v = std::any_cast<std::vector<scripting::Color3>>(&value)) {
			return join(*v, [](const scripting::Color3& e) { return std::format("{} {} {}", e.rgb.r, e.rgb.g, e.rgb.b); });
		}
		if (const auto* v = std::any_cast<std::vector<scripting::Color4>>(&value)) {
			return join(*v, [](const scripting::Color4& e) {
				return std::format("{} {} {} {}", e.rgba.r, e.rgba.g, e.rgba.b, e.rgba.a);
			});
		}
		if (const auto* v = std::any_cast<std::vector<toast::Box<toast::Node>>>(&value)) {
			return join(*v, [](const toast::Box<toast::Node>& e) { return e.exists() ? e->uid().get() : std::string {}; });
		}
		if (const auto* v = std::any_cast<std::vector<scripting::AssetProxy>>(&value)) {
			return join(*v, [](const scripting::AssetProxy& e) { return e.uid().data() != 0 ? e.uid().get() : std::string {}; });
		}
		return "";
	}

	switch (desc.kind) {
		case LuaVarKind::boolean:
			if (const auto* v = std::any_cast<bool>(&value)) {
				return *v ? "true" : "false";
			}
			break;
		case LuaVarKind::integer:
			if (const auto* v = std::any_cast<int>(&value)) {
				return std::format("{}", *v);
			}
			if (const auto* v = std::any_cast<double>(&value)) {
				return std::format("{}", *v);
			}
			break;
		case LuaVarKind::number:
			if (const auto* v = std::any_cast<double>(&value)) {
				return std::format("{}", *v);
			}
			if (const auto* v = std::any_cast<int>(&value)) {
				return std::format("{}", *v);
			}
			break;
		case LuaVarKind::string:
			if (const auto* v = std::any_cast<std::string>(&value)) {
				return *v;
			}
			break;
		case LuaVarKind::vec2:
			if (const auto* v = std::any_cast<glm::vec2>(&value)) {
				return std::format("{} {}", v->x, v->y);
			}
			break;
		case LuaVarKind::vec3:
			if (const auto* v = std::any_cast<glm::vec3>(&value)) {
				return std::format("{} {} {}", v->x, v->y, v->z);
			}
			break;
		case LuaVarKind::vec4:
			if (const auto* v = std::any_cast<glm::vec4>(&value)) {
				return std::format("{} {} {} {}", v->x, v->y, v->z, v->w);
			}
			break;
		case LuaVarKind::color3:
			if (const auto* v = std::any_cast<scripting::Color3>(&value)) {
				return std::format("{} {} {}", v->rgb.r, v->rgb.g, v->rgb.b);
			}
			break;
		case LuaVarKind::color4:
			if (const auto* v = std::any_cast<scripting::Color4>(&value)) {
				return std::format("{} {} {} {}", v->rgba.r, v->rgba.g, v->rgba.b, v->rgba.a);
			}
			break;
		case LuaVarKind::node_ref:
			if (const auto* v = std::any_cast<toast::Box<toast::Node>>(&value)) {
				return v->exists() ? (*v)->uid().get() : "";
			}
			break;
		case LuaVarKind::asset_ref:
			if (const auto* v = std::any_cast<scripting::AssetProxy>(&value)) {
				return v->uid().data() != 0 ? v->uid().get() : "";
			}
			break;
	}
	return "";
}

auto parseLuaValue(const LuaVarDesc& desc, std::string_view text, const NodeResolver& find_node) -> std::any {
	auto parse_uid = [](std::string_view s) { return s.empty() ? toast::UID {0} : toast::UID::fromString(std::string(s)); };

	if (desc.is_array) {
		const std::vector<float> floats = parseFloats(text);
		auto group = [&](size_t stride, auto&& make) {
			using T = decltype(make(0));
			std::vector<T> out;
			for (size_t i = 0; i + stride <= floats.size(); i += stride) {
				out.push_back(make(i));
			}
			return std::any {std::move(out)};
		};
		auto tokens = [&](char sep) {
			std::vector<std::string> out;
			size_t start = 0;
			while (start <= text.size()) {
				const size_t end = text.find(sep, start);
				const auto piece = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
				if (!piece.empty() || sep == '\x1f') {
					out.emplace_back(piece);
				}
				if (end == std::string_view::npos) {
					break;
				}
				start = end + 1;
			}
			return out;
		};

		switch (desc.kind) {
			case LuaVarKind::boolean: {
				std::vector<bool> out;
				for (const auto& t : tokens(' ')) {
					out.push_back(t == "true");
				}
				return out;
			}
			case LuaVarKind::integer: {
				std::vector<int> out;
				out.reserve(floats.size());
				for (float f : floats) {
					out.push_back(static_cast<int>(f));
				}
				return out;
			}
			case LuaVarKind::number: {
				std::vector<double> out;
				out.reserve(floats.size());
				for (float f : floats) {
					out.push_back(f);
				}
				return out;
			}
			case LuaVarKind::string: return text.empty() ? std::any {std::vector<std::string> {}} : std::any {tokens('\x1f')};
			case LuaVarKind::vec2: return group(2, [&](size_t i) { return glm::vec2(floats[i], floats[i + 1]); });
			case LuaVarKind::vec3: return group(3, [&](size_t i) { return glm::vec3(floats[i], floats[i + 1], floats[i + 2]); });
			case LuaVarKind::vec4:
				return group(4, [&](size_t i) { return glm::vec4(floats[i], floats[i + 1], floats[i + 2], floats[i + 3]); });
			case LuaVarKind::color3:
				return group(3, [&](size_t i) { return scripting::Color3(floats[i], floats[i + 1], floats[i + 2]); });
			case LuaVarKind::color4:
				return group(4, [&](size_t i) { return scripting::Color4(floats[i], floats[i + 1], floats[i + 2], floats[i + 3]); });
			case LuaVarKind::node_ref: {
				std::vector<toast::Box<toast::Node>> out;
				for (const auto& t : tokens(' ')) {
					out.push_back(find_node(t));
				}
				return out;
			}
			case LuaVarKind::asset_ref: {
				std::vector<scripting::AssetProxy> out;
				for (const auto& t : tokens(' ')) {
					const toast::UID uid = parse_uid(t);
					out.push_back(uid.data() != 0 ? scripting::AssetProxy(uid) : scripting::AssetProxy(assets::HandleBase(nullptr)));
				}
				return out;
			}
		}
		return {};
	}

	switch (desc.kind) {
		case LuaVarKind::boolean: return text == "true";
		case LuaVarKind::integer: {
			int v = 0;
			auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
			return ec == std::errc {} ? std::any {v} : std::any {};
		}
		case LuaVarKind::number: {
			const std::vector<float> f = parseFloats(text);
			return f.empty() ? std::any {} : std::any {static_cast<double>(f[0])};
		}
		case LuaVarKind::string: return std::string(text);
		case LuaVarKind::vec2: {
			const auto f = parseFloats(text);
			return f.size() >= 2 ? std::any {glm::vec2(f[0], f[1])} : std::any {};
		}
		case LuaVarKind::vec3: {
			const auto f = parseFloats(text);
			return f.size() >= 3 ? std::any {glm::vec3(f[0], f[1], f[2])} : std::any {};
		}
		case LuaVarKind::vec4: {
			const auto f = parseFloats(text);
			return f.size() >= 4 ? std::any {glm::vec4(f[0], f[1], f[2], f[3])} : std::any {};
		}
		case LuaVarKind::color3: {
			const auto f = parseFloats(text);
			return f.size() >= 3 ? std::any {scripting::Color3(f[0], f[1], f[2])} : std::any {};
		}
		case LuaVarKind::color4: {
			const auto f = parseFloats(text);
			return f.size() >= 4 ? std::any {scripting::Color4(f[0], f[1], f[2], f[3])} : std::any {};
		}
		case LuaVarKind::node_ref: return find_node(text);
		case LuaVarKind::asset_ref: {
			const toast::UID uid = parse_uid(text);
			if (uid.data() == 0) {
				return scripting::AssetProxy(assets::HandleBase(nullptr));
			}
			return scripting::AssetProxy(uid);
		}
	}
	return {};
}

}
