#include "cook.hpp"
#include "registry.hpp"
#include "text.hpp"

#include <cctype>
#include <ffi/settings.h>
#include <mutex>
#include <unordered_map>

namespace {

using namespace toast::params;

thread_local std::string g_string_return;
thread_local std::string g_error;

constexpr auto k_write_wait = std::chrono::milliseconds(250);

constexpr std::string_view k_layer_names = "physics.layers.names";
constexpr std::string_view k_layer_abbreviations = "physics.layers.abbreviations";

struct Cached {
	std::string path;
	std::string label;
	std::string category;
	std::string description;
};

std::mutex g_cache_mutex;
std::unordered_map<std::string, Cached> g_cache;

auto titleCase(std::string text) -> std::string {
	for (char& c : text) {
		if (c == '_') {
			c = ' ';
		}
	}
	if (!text.empty()) {
		text.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(text.front())));
	}
	return text;
}

auto pathOf(const ParamBase& param) -> std::string {
	return std::string {param.group()->name()} + "." + std::string {param.id()};
}

auto cachedFor(const ParamBase& param) -> const Cached& {
	const std::lock_guard lock(g_cache_mutex);
	auto path = pathOf(param);
	auto it = g_cache.find(path);
	if (it != g_cache.end()) {
		return it->second;
	}
	const std::string_view id = param.id();
	const size_t dot = id.rfind('.');
	Cached cached;
	cached.path = path;
	cached.label = param.name();
	cached.category = titleCase(std::string {dot == std::string_view::npos ? param.group()->name() : id.substr(0, id.find('.'))});
	cached.description = param.description();
	return g_cache.emplace(std::move(path), std::move(cached)).first->second;
}

auto allParams() -> std::vector<ParamBase*> {
	std::vector<ParamBase*> all;
	for (const GroupBase* group : Registry::get().groups()) {
		for (ParamBase* param : group->params()) {
			all.push_back(param);
		}
	}
	return all;
}

auto paramAt(int32_t index) -> ParamBase* {
	if (index < 0) {
		return nullptr;
	}
	const auto all = allParams();
	return static_cast<size_t>(index) < all.size() ? all[static_cast<size_t>(index)] : nullptr;
}

auto ffiType(Type type) -> int32_t {
	switch (type) {
		case Type::boolean: return TOAST_SETTING_BOOL;
		case Type::integer: return TOAST_SETTING_INT;
		case Type::floating: return TOAST_SETTING_FLOAT;
		case Type::string:
		case Type::asset: return TOAST_SETTING_STRING;
		case Type::vec2: return TOAST_SETTING_VEC2;
		case Type::vec3: return TOAST_SETTING_VEC3;
		case Type::vec4: return TOAST_SETTING_VEC4;
		case Type::quat: return TOAST_SETTING_QUAT;
	}
	return TOAST_SETTING_STRING;
}

auto drawableAsScalar(const ParamBase& param) -> bool {
	if (param.isArray()) {
		return false;
	}
	switch (param.type()) {
		case Type::vec2:
		case Type::vec3:
		case Type::vec4:
		case Type::quat: return false;
		default: return true;
	}
}

auto toDouble(const std::optional<Scalar>& scalar) -> double {
	if (!scalar) {
		return 0.0;
	}
	if (const auto* d = std::get_if<double>(&*scalar)) {
		return *d;
	}
	if (const auto* i = std::get_if<int64_t>(&*scalar)) {
		return static_cast<double>(*i);
	}
	return 0.0;
}

auto currentValue(const std::string& path) -> std::optional<Value> {
	auto& registry = Registry::get();
	const ParamBase* param = registry.find(path);
	if (param == nullptr) {
		return std::nullopt;
	}
	if (auto pending = registry.pendingValue(path)) {
		return pending;
	}
	return param->snapshot();
}

struct LayerKey {
	std::string_view array;
	size_t index;
};

auto parseLayerKey(std::string_view key) -> std::optional<LayerKey> {
	constexpr std::string_view k_prefix = "physics.layers.";
	if (!key.starts_with(k_prefix)) {
		return std::nullopt;
	}
	key.remove_prefix(k_prefix.size());
	const size_t dot = key.find('.');
	if (dot == std::string_view::npos || dot == 0) {
		return std::nullopt;
	}
	size_t index = 0;
	for (const char c : key.substr(0, dot)) {
		if (c < '0' || c > '9') {
			return std::nullopt;
		}
		index = index * 10 + static_cast<size_t>(c - '0');
	}
	const std::string_view field = key.substr(dot + 1);
	if (field == "name") {
		return LayerKey {k_layer_names, index};
	}
	if (field == "abbr") {
		return LayerKey {k_layer_abbreviations, index};
	}
	return std::nullopt;
}

auto layerElement(const LayerKey& key) -> std::string {
	const auto value = currentValue(std::string {key.array});
	if (!value) {
		return {};
	}
	const auto* items = std::get_if<std::vector<Scalar>>(&*value);
	if (items == nullptr || key.index >= items->size()) {
		return {};
	}
	const auto* text = std::get_if<std::string>(&(*items)[key.index]);
	return text != nullptr ? *text : std::string {};
}

auto authoringLayerFor(const ParamBase& param) -> Layer {
	return param.kind() == Kind::user ? Layer::saved : Registry::get().authoringLayer();
}

void settle() {
	Registry::get().waitForApply(k_write_wait);
}

void writeLayerElement(const LayerKey& key, std::optional<std::string> text) {
	auto& registry = Registry::get();
	const ParamBase* param = registry.find(key.array);
	if (param == nullptr) {
		return;
	}
	std::vector<Scalar> items;
	if (const auto current = currentValue(std::string {key.array});
	    current && std::holds_alternative<std::vector<Scalar>>(*current)) {
		items = std::get<std::vector<Scalar>>(*current);
	}
	const auto defaults = std::get<std::vector<Scalar>>(param->defaultValue());
	if (items.size() < defaults.size()) {
		items.insert(items.end(), defaults.begin() + static_cast<std::ptrdiff_t>(items.size()), defaults.end());
	}
	if (key.index >= items.size()) {
		return;
	}
	items[key.index] = text ? Scalar {*text} : defaults[key.index];
	registry.request(std::string {key.array}, Value {std::move(items)}, authoringLayerFor(*param));
	settle();
}

auto coerce(const ParamBase& param, Scalar input) -> std::optional<Value> {
	if (param.isArray()) {
		return std::nullopt;
	}
	switch (param.type()) {
		case Type::boolean:
			if (std::holds_alternative<bool>(input)) {
				return Value {std::move(input)};
			}
			break;
		case Type::integer:
			if (std::holds_alternative<int64_t>(input)) {
				return Value {std::move(input)};
			}
			break;
		case Type::floating:
			if (std::holds_alternative<double>(input)) {
				return Value {std::move(input)};
			}
			if (const auto* integer = std::get_if<int64_t>(&input)) {
				return Value {Scalar {static_cast<double>(*integer)}};
			}
			break;
		case Type::string:
			if (std::holds_alternative<std::string>(input)) {
				return Value {std::move(input)};
			}
			break;
		case Type::asset:
			if (const auto* text = std::get_if<std::string>(&input)) {
				return Value {Scalar {AssetRef {*text}}};
			}
			break;
		default: break;
	}
	return std::nullopt;
}

void write(const char* key, Scalar input) {
	if (key == nullptr) {
		return;
	}
	auto& registry = Registry::get();
	ParamBase* param = registry.find(key);
	if (param == nullptr) {
		return;
	}
	auto value = coerce(*param, std::move(input));
	if (!value || !param->accepts(*value)) {
		return;
	}
	registry.request(key, std::move(*value), authoringLayerFor(*param));
	settle();
}

auto readScalar(const char* key) -> std::optional<Scalar> {
	if (key == nullptr) {
		return std::nullopt;
	}
	const auto value = currentValue(key);
	if (!value) {
		return std::nullopt;
	}
	if (const auto* scalar = std::get_if<Scalar>(&*value)) {
		return *scalar;
	}
	return std::nullopt;
}

}

extern "C" {

auto toast_settings_count() noexcept -> int32_t {
	return static_cast<int32_t>(allParams().size());
}

auto toast_settings_get_desc(int32_t index, toast_setting_desc* out) noexcept -> int32_t {
	const ParamBase* param = paramAt(index);
	if (param == nullptr || out == nullptr) {
		return 0;
	}
	const Cached& cached = cachedFor(*param);

	out->key = cached.path.c_str();
	out->label = cached.label.c_str();
	out->category = cached.category.c_str();
	out->description = cached.description.c_str();
	out->type = ffiType(param->type());
	out->min = toDouble(param->minValue());
	out->max = toDouble(param->maxValue());
	out->step = 0.0;
	out->option_count = static_cast<int32_t>(param->options().size());
	out->requires_restart = param->requiresRestart() ? 1 : 0;
	out->hidden = drawableAsScalar(*param) ? 0 : 1;
	out->kind = static_cast<int32_t>(param->kind());
	out->is_array = param->isArray() ? 1 : 0;
	out->value_type = static_cast<int32_t>(param->type());
	return 1;
}

auto toast_settings_get_asset_type(int32_t index) noexcept -> const char* {
	const ParamBase* param = paramAt(index);
	if (param == nullptr || param->type() != Type::asset || param->isArray()) {
		return "";
	}
	return param->assetType().data() != nullptr ? param->assetType().data() : "";
}

auto toast_settings_get_unit(int32_t index) noexcept -> const char* {
	const ParamBase* param = paramAt(index);
	return param != nullptr && param->unit().data() != nullptr ? param->unit().data() : "";
}

auto toast_settings_get_option(int32_t index, int32_t option) noexcept -> const char* {
	const ParamBase* param = paramAt(index);
	if (param == nullptr || option < 0 || static_cast<size_t>(option) >= param->options().size()) {
		return nullptr;
	}
	return param->options()[static_cast<size_t>(option)].data();
}

auto toast_settings_get_bool(const char* key) noexcept -> int32_t {
	const auto scalar = readScalar(key);
	const auto* typed = scalar ? std::get_if<bool>(&*scalar) : nullptr;
	return typed != nullptr && *typed ? 1 : 0;
}

auto toast_settings_get_int(const char* key) noexcept -> int64_t {
	const auto scalar = readScalar(key);
	const auto* typed = scalar ? std::get_if<int64_t>(&*scalar) : nullptr;
	return typed != nullptr ? *typed : 0;
}

auto toast_settings_get_float(const char* key) noexcept -> double {
	const auto scalar = readScalar(key);
	if (!scalar) {
		return 0.0;
	}
	if (const auto* d = std::get_if<double>(&*scalar)) {
		return *d;
	}
	if (const auto* i = std::get_if<int64_t>(&*scalar)) {
		return static_cast<double>(*i);
	}
	return 0.0;
}

auto toast_settings_get_string(const char* key) noexcept -> const char* {
	g_string_return.clear();
	if (key != nullptr) {
		if (const auto layer_key = parseLayerKey(key)) {
			g_string_return = layerElement(*layer_key);
		} else if (const auto scalar = readScalar(key)) {
			if (const auto* text = std::get_if<std::string>(&*scalar)) {
				g_string_return = *text;
			} else if (const auto* asset = std::get_if<AssetRef>(&*scalar)) {
				g_string_return = asset->uri;
			}
		}
	}
	return g_string_return.c_str();
}

auto toast_settings_get_text(const char* key) noexcept -> const char* {
	g_string_return.clear();
	if (key != nullptr) {
		const ParamBase* param = Registry::get().find(key);
		if (const auto value = currentValue(key); param != nullptr && value) {
			g_string_return = toText(*value, param->type(), param->options());
		}
	}
	return g_string_return.c_str();
}

auto toast_settings_get_default_text(const char* key) noexcept -> const char* {
	g_string_return.clear();
	if (key != nullptr) {
		if (const ParamBase* param = Registry::get().find(key); param != nullptr) {
			g_string_return = toText(param->defaultValue(), param->type(), param->options());
		}
	}
	return g_string_return.c_str();
}

auto toast_settings_set_text(const char* key, const char* text) noexcept -> int32_t {
	g_error.clear();
	if (key == nullptr || text == nullptr) {
		g_error = "no key or no text";
		return 0;
	}
	auto& registry = Registry::get();
	ParamBase* param = registry.find(key);
	if (param == nullptr) {
		g_error = std::string {"no parameter '"} + key + "'";
		return 0;
	}
	auto value = fromText(text, param->type(), param->isArray(), param->options());
	if (!value) {
		g_error = value.error();
		return 0;
	}
	if (!param->accepts(*value)) {
		g_error = "that is not a value it can take";
		return 0;
	}
	registry.request(key, std::move(*value), authoringLayerFor(*param));
	settle();
	return 1;
}

auto toast_settings_last_error() noexcept -> const char* {
	return g_error.c_str();
}

auto toast_settings_is_overridden(const char* key) noexcept -> int32_t {
	if (key == nullptr) {
		return 0;
	}
	const ParamBase* param = Registry::get().find(key);
	if (param == nullptr) {
		return 0;
	}
	const auto current = currentValue(key);
	if (!current) {
		return 0;
	}
	return toText(*current, param->type(), param->options()) != toText(param->defaultValue(), param->type(), param->options()) ? 1
	                                                                                                                           : 0;
}

void toast_settings_set_bool(const char* key, int32_t value) noexcept {
	write(key, Scalar {value != 0});
}

void toast_settings_set_int(const char* key, int64_t value) noexcept {
	write(key, Scalar {value});
}

void toast_settings_set_float(const char* key, double value) noexcept {
	write(key, Scalar {value});
}

void toast_settings_set_string(const char* key, const char* value) noexcept {
	if (key == nullptr || value == nullptr) {
		return;
	}
	if (const auto layer_key = parseLayerKey(key)) {
		writeLayerElement(*layer_key, std::string {value});
		return;
	}
	write(key, Scalar {std::string {value}});
}

void toast_settings_reset(const char* key) noexcept {
	if (key == nullptr) {
		return;
	}
	if (const auto layer_key = parseLayerKey(key)) {
		writeLayerElement(*layer_key, std::nullopt);
		return;
	}
	auto& registry = Registry::get();
	if (const ParamBase* param = registry.find(key); param != nullptr) {
		registry.requestClear(key, authoringLayerFor(*param));
		settle();
	}
}

void toast_settings_reset_all() noexcept {
	auto& registry = Registry::get();
	for (const ParamBase* param : allParams()) {
		const Layer layer = authoringLayerFor(*param);
		if (param->layerValue(layer).has_value()) {
			registry.requestClear(pathOf(*param), layer);
		}
	}
	settle();
}

auto toast_settings_save() noexcept -> int32_t {
	Registry::get().requestSave();
	return Registry::get().waitForApply(std::chrono::milliseconds(2000)) ? 1 : 0;
}

auto toast_settings_is_dirty() noexcept -> int32_t {
	return Registry::get().dirtyGroups().empty() ? 0 : 1;
}

auto toast_settings_active_path() noexcept -> const char* {
	g_string_return = Registry::get().layerPath(Registry::get().authoringLayer());
	return g_string_return.c_str();
}

void toast_settings_restore() noexcept {
	Registry::get().requestRestore();
	Registry::get().waitForApply(std::chrono::milliseconds(2000));
}

auto toast_settings_cook(const char* config_dir, const char* toast_file, const char* stage_dir) noexcept -> int32_t {
	if (config_dir == nullptr || toast_file == nullptr || stage_dir == nullptr) {
		return 0;
	}
	try {
		return cookConfigs(config_dir, toast_file, stage_dir) ? 1 : 0;
	} catch (...) { return 0; }
}
}
