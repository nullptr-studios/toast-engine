#include "settings.hpp"

#include <fstream>
#include <toast/log.hpp>
#include <toml++/toml.hpp>
#include <tracy/Tracy.hpp>

namespace toast::settings {
namespace {

constexpr const char* k_sink = "Settings";

auto layerIndex(Layer layer) -> size_t {
	return static_cast<size_t>(layer) - 1;
}

auto typeOf(const Value& value) -> Type {
	if (std::holds_alternative<bool>(value)) {
		return Type::boolean;
	}
	if (std::holds_alternative<int64_t>(value)) {
		return Type::integer;
	}
	if (std::holds_alternative<double>(value)) {
		return Type::floating;
	}
	return Type::string;
}

/// @brief Converts @p value to @p type, or nullopt when the two describe different things
///
/// TOML gives back whatever the file literally contained, so a float setting written as `1` parses as an
/// integer and would otherwise be rejected on load. Numeric widening is allowed for that reason; a string
/// against a number is not, because that is a genuinely wrong file rather than a formatting accident
auto coerce(const Value& value, Type type) -> std::optional<Value> {
	switch (type) {
		case Type::boolean:
			if (const auto* b = std::get_if<bool>(&value)) {
				return Value {*b};
			}
			if (const auto* i = std::get_if<int64_t>(&value)) {
				return Value {*i != 0};
			}
			return std::nullopt;
		case Type::integer:
			if (const auto* i = std::get_if<int64_t>(&value)) {
				return Value {*i};
			}
			if (const auto* b = std::get_if<bool>(&value)) {
				return Value {static_cast<int64_t>(*b)};
			}
			if (const auto* d = std::get_if<double>(&value)) {
				return Value {static_cast<int64_t>(*d)};
			}
			return std::nullopt;
		case Type::floating:
			if (const auto* d = std::get_if<double>(&value)) {
				return Value {*d};
			}
			if (const auto* i = std::get_if<int64_t>(&value)) {
				return Value {static_cast<double>(*i)};
			}
			return std::nullopt;
		case Type::string:
			if (const auto* s = std::get_if<std::string>(&value)) {
				return Value {*s};
			}
			return std::nullopt;
	}
	return std::nullopt;
}

auto sameValue(const Value& a, const Value& b) -> bool {
	return a == b;
}

/// @brief Splits "renderer.shadows.resolution" into its table path and leaf key
auto splitKey(std::string_view key) -> std::pair<std::vector<std::string_view>, std::string_view> {
	std::vector<std::string_view> path;
	size_t start = 0;
	while (true) {
		const size_t dot = key.find('.', start);
		if (dot == std::string_view::npos) {
			break;
		}
		path.emplace_back(key.substr(start, dot - start));
		start = dot + 1;
	}
	return {path, key.substr(start)};
}

/// @brief Walks @p root down @p path, reading through the nesting a key's dots describe
auto tomlLookup(const toml::table& root, std::string_view key) -> const toml::node* {
	const auto [path, leaf] = splitKey(key);
	const toml::table* table = &root;
	for (const auto& segment : path) {
		const auto* child = table->get(segment);
		if (child == nullptr || !child->is_table()) {
			return nullptr;
		}
		table = child->as_table();
	}
	return table->get(leaf);
}

void tomlInsert(toml::table& root, std::string_view key, const Value& value) {
	const auto [path, leaf] = splitKey(key);
	toml::table* table = &root;
	for (const auto& segment : path) {
		auto* child = table->get(segment);
		if (child == nullptr || !child->is_table()) {
			table->insert_or_assign(segment, toml::table {});
			child = table->get(segment);
		}
		table = child->as_table();
	}
	std::visit([&](const auto& v) { table->insert_or_assign(leaf, v); }, value);
}

/// @brief Flattens a parsed file into dotted keys, so a caller never walks the nesting itself
void flatten(const toml::table& table, const std::string& prefix, std::unordered_map<std::string, Value>& out) {
	for (const auto& [key, node] : table) {
		const std::string full = prefix.empty() ? std::string {key.str()} : prefix + "." + std::string {key.str()};
		if (const auto* child = node.as_table()) {
			flatten(*child, full, out);
		} else if (const auto* b = node.as_boolean()) {
			out.insert_or_assign(full, Value {b->get()});
		} else if (const auto* i = node.as_integer()) {
			out.insert_or_assign(full, Value {i->get()});
		} else if (const auto* d = node.as_floating_point()) {
			out.insert_or_assign(full, Value {d->get()});
		} else if (const auto* s = node.as_string()) {
			out.insert_or_assign(full, Value {s->get()});
		}
	}
}

auto defaultCategory(std::string_view key) -> std::string {
	const auto dot = key.rfind('.');
	return dot == std::string_view::npos ? std::string {} : std::string {key.substr(0, dot)};
}

auto defaultLabel(std::string_view key) -> std::string {
	const auto dot = key.rfind('.');
	return std::string {dot == std::string_view::npos ? key : key.substr(dot + 1)};
}

}

auto Settings::get() -> Settings& {
	static Settings instance;
	return instance;
}

auto Settings::declare(std::string_view key, Type type, Value default_value, Meta meta) -> Entry* {
	std::scoped_lock lock(m_mutex);

	const std::string key_str {key};
	if (const auto it = m_by_key.find(key_str); it != m_by_key.end()) {
		TOAST_WARN(k_sink, "Setting '{}' declared twice; keeping the first declaration", key_str);
		return it->second;
	}

	if (meta.label.empty()) {
		meta.label = defaultLabel(key);
	}
	if (meta.category.empty()) {
		meta.category = defaultCategory(key);
	}

	Entry& entry = m_entries.emplace_back();
	entry.key = key_str;
	entry.type = type;
	entry.meta = std::move(meta);
	entry.default_value = std::move(default_value);

	// Anything the files already carried for this key, held since load() could not know its type yet
	for (size_t layer = 0; layer < k_layer_count; ++layer) {
		const auto it = m_pending[layer].find(key_str);
		if (it == m_pending[layer].end()) {
			continue;
		}
		if (auto converted = coerce(it->second, type)) {
			entry.overrides[layer] = std::move(*converted);
		} else {
			TOAST_WARN(k_sink, "Setting '{}' has the wrong type on disk; ignoring the stored value", key_str);
		}
		m_pending[layer].erase(it);
	}

	resolveEntry(entry);
	m_by_key.emplace(key_str, &entry);
	return &entry;
}

auto Settings::declareBool(std::string_view key, bool default_value, Meta meta) -> Entry* {
	return declare(key, Type::boolean, Value {default_value}, std::move(meta));
}

auto Settings::declareInt(std::string_view key, int64_t default_value, Meta meta) -> Entry* {
	return declare(key, Type::integer, Value {default_value}, std::move(meta));
}

auto Settings::declareFloat(std::string_view key, double default_value, Meta meta) -> Entry* {
	return declare(key, Type::floating, Value {default_value}, std::move(meta));
}

auto Settings::declareString(std::string_view key, std::string default_value, Meta meta) -> Entry* {
	return declare(key, Type::string, Value {std::move(default_value)}, std::move(meta));
}

void Settings::resolveEntry(Entry& entry) {
	entry.resolved = entry.default_value;
	for (const auto& override : entry.overrides) {
		if (override.has_value()) {
			entry.resolved = *override;
		}
	}
}

auto Settings::find(std::string_view key) -> Entry* {
	std::scoped_lock lock(m_mutex);
	const auto it = m_by_key.find(key);
	return it == m_by_key.end() ? nullptr : it->second;
}

auto Settings::entries() -> std::vector<Entry*> {
	std::scoped_lock lock(m_mutex);
	std::vector<Entry*> out;
	out.reserve(m_entries.size());
	for (auto& entry : m_entries) {
		out.push_back(&entry);
	}
	return out;
}

auto Settings::value(std::string_view key) -> std::optional<Value> {
	std::scoped_lock lock(m_mutex);
	const auto it = m_by_key.find(key);
	if (it == m_by_key.end()) {
		return std::nullopt;
	}
	return it->second->resolved;
}

auto Settings::resolved(const Entry* entry) -> Value {
	if (entry == nullptr) {
		return Value {};
	}
	std::scoped_lock lock(m_mutex);
	return entry->resolved;
}

auto Settings::setValue(std::string_view key, const Value& value) -> bool {
	std::vector<std::function<void(const Value&)>> callbacks;
	Value new_value;
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_by_key.find(key);
		if (it == m_by_key.end()) {
			TOAST_WARN(k_sink, "setValue on undeclared setting '{}'", key);
			return false;
		}

		Entry& entry = *it->second;
		auto converted = coerce(value, entry.type);
		if (!converted.has_value()) {
			TOAST_WARN(
			    k_sink, "setValue on '{}' with a {} value; expected a different type", entry.key, static_cast<int>(typeOf(value))
			);
			return false;
		}

		const Value previous = entry.resolved;
		entry.overrides[layerIndex(m_active_layer)] = *converted;
		resolveEntry(entry);
		if (sameValue(previous, entry.resolved)) {
			return true;
		}

		m_dirty = true;
		new_value = entry.resolved;
		callbacks = entry.on_change;
	}

	// Outside the lock: a callback may legitimately read other settings, and holding a recursive mutex across
	// arbitrary engine code is how a settings change ends up owning a lock the renderer wanted
	for (const auto& callback : callbacks) {
		callback(new_value);
	}
	return true;
}

auto Settings::reset(std::string_view key) -> bool {
	std::vector<std::function<void(const Value&)>> callbacks;
	Value new_value;
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_by_key.find(key);
		if (it == m_by_key.end()) {
			return false;
		}

		Entry& entry = *it->second;
		if (!entry.overrides[layerIndex(m_active_layer)].has_value()) {
			return true;
		}

		const Value previous = entry.resolved;
		entry.overrides[layerIndex(m_active_layer)].reset();
		resolveEntry(entry);
		m_dirty = true;
		if (sameValue(previous, entry.resolved)) {
			return true;
		}
		new_value = entry.resolved;
		callbacks = entry.on_change;
	}

	for (const auto& callback : callbacks) {
		callback(new_value);
	}
	return true;
}

void Settings::resetAll() {
	std::vector<std::string> keys;
	{
		std::scoped_lock lock(m_mutex);
		keys.reserve(m_entries.size());
		for (const auto& entry : m_entries) {
			keys.push_back(entry.key);
		}
	}
	for (const auto& key : keys) {
		reset(key);
	}
}

void Settings::setPaths(std::filesystem::path project_file, std::filesystem::path user_file) {
	std::scoped_lock lock(m_mutex);
	m_project_path = std::move(project_file);
	m_user_path = std::move(user_file);
}

void Settings::setActiveLayer(Layer layer) {
	std::scoped_lock lock(m_mutex);
	m_active_layer = layer == Layer::code ? Layer::user : layer;
}

auto Settings::loadFile(const std::filesystem::path& path, Layer layer) -> bool {
	ZoneScoped;
	if (path.empty() || !std::filesystem::exists(path)) {
		return false;
	}

	toml::table table;
	try {
		table = toml::parse_file(path.string());
	} catch (const std::exception& e) {
		TOAST_ERROR(k_sink, "Failed to parse {}: {}", path.string(), e.what());
		return false;
	}

	std::unordered_map<std::string, Value> flat;
	flatten(table, {}, flat);

	const size_t index = layerIndex(layer);
	m_pending[index].clear();
	for (auto& entry : m_entries) {
		entry.overrides[index].reset();
	}

	size_t applied = 0;
	for (auto& [key, value] : flat) {
		const auto it = m_by_key.find(key);
		if (it == m_by_key.end()) {
			m_pending[index].insert_or_assign(key, value);
			continue;
		}
		if (auto converted = coerce(value, it->second->type)) {
			it->second->overrides[index] = std::move(*converted);
			++applied;
		} else {
			TOAST_WARN(k_sink, "Setting '{}' has the wrong type in {}; ignoring it", key, path.string());
		}
	}

	for (auto& entry : m_entries) {
		resolveEntry(entry);
	}

	TOAST_INFO(k_sink, "Loaded {} ({} applied, {} held for undeclared keys)", path.string(), applied, m_pending[index].size());
	return true;
}

void Settings::load() {
	std::scoped_lock lock(m_mutex);
	loadFile(m_project_path, Layer::project);
	loadFile(m_user_path, Layer::user);
	m_dirty = false;
}

auto Settings::save() -> bool {
	ZoneScoped;
	std::scoped_lock lock(m_mutex);

	const auto& path = m_active_layer == Layer::project ? m_project_path : m_user_path;
	if (path.empty()) {
		TOAST_WARN(
		    k_sink, "No path configured for the {} layer; nothing saved", m_active_layer == Layer::project ? "project" : "user"
		);
		return false;
	}

	const size_t index = layerIndex(m_active_layer);
	toml::table root;
	if (m_active_layer == Layer::project && std::filesystem::exists(path)) {
		try {
			root = toml::parse_file(path.string());
		} catch (const std::exception& e) {
			TOAST_ERROR(k_sink, "Failed to parse {} before saving: {}", path.string(), e.what());
			return false;
		}
	}

	for (const auto& entry : m_entries) {
		if (m_active_layer == Layer::project) {
			tomlInsert(root, entry.key, entry.overrides[index].value_or(entry.default_value));
			continue;
		}

		if (!entry.overrides[index].has_value()) {
			continue;
		}

		// What the value would be with this layer's override removed. Writing only genuine differences is what
		// keeps a user file to the few things a player actually changed, and lets a later patch move a default
		// they never touched instead of freezing today's value into their file forever
		Value underneath = entry.default_value;
		for (size_t below = 0; below < index; ++below) {
			if (entry.overrides[below].has_value()) {
				underneath = *entry.overrides[below];
			}
		}
		if (sameValue(underneath, *entry.overrides[index])) {
			continue;
		}
		tomlInsert(root, entry.key, *entry.overrides[index]);
	}

	// Keys nothing declared this run are written back out rather than dropped, so a settings file is never
	// pruned by a build whose systems happened not to be constructed
	for (const auto& [key, value] : m_pending[index]) {
		tomlInsert(root, key, value);
	}

	try {
		if (path.has_parent_path()) {
			std::filesystem::create_directories(path.parent_path());
		}
		std::ofstream out(path, std::ios::trunc);
		if (!out) {
			TOAST_ERROR(k_sink, "Could not open {} for writing", path.string());
			return false;
		}
		out << root;
		out << '\n';
	} catch (const std::exception& e) {
		TOAST_ERROR(k_sink, "Failed to write {}: {}", path.string(), e.what());
		return false;
	}

	m_dirty = false;
	TOAST_INFO(k_sink, "Saved {}", path.string());
	return true;
}

auto Settings::saveIfDirty() -> bool {
	{
		std::scoped_lock lock(m_mutex);
		if (!m_dirty) {
			return true;
		}
	}
	return save();
}

}
