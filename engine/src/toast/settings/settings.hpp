/**
 * @file settings.hpp
 * @author dario
 * @date 16/08/2026
 */

#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <unordered_map>
#include <variant>
#include <vector>

namespace toast::settings {

/// @brief Storage type of a setting; the editor picks its widget from this
enum class Type : uint8_t {
	boolean,
	integer,
	floating,
	string
};

/**
 * @brief Where a value comes from, lowest priority first
 *
 * `code` is the default passed to declare() and is never written to disk. `project` is authored in the editor
 * and ships with the game. `user` is what a player's own tweaks land in, so a patch that changes a project
 * default still reaches players who never touched that setting
 *
 * A resolved value is the highest layer that has an override, so writing one layer never destroys another
 */
enum class Layer : uint8_t {
	code,
	project,
	user
};

inline constexpr size_t k_layer_count = 2;    ///< Writable layers; `code` lives in Entry::default_value

using Value = std::variant<bool, int64_t, double, std::string>;

/**
 * @brief Everything the editor needs to render a setting without knowing what it is
 *
 * The point of carrying this on the setting rather than in the editor: a system declares its settings next to
 * the code that reads them, and the settings window picks them up with no C# change. @p category is the
 * grouping header, defaulting to the key minus its last segment
 */
struct Meta {
	std::string label;                   ///< Display name; the key's last segment when empty
	std::string category;                ///< Grouping header; the key minus its last segment when empty
	std::string description;             ///< Tooltip
	double min = 0.0;                    ///< Slider bounds; equal min and max mean unbounded
	double max = 0.0;
	double step = 0.0;                   ///< Drag increment; the editor picks one when 0
	std::vector<std::string> options;    ///< Non-empty makes an integer setting a combo box indexing this
	bool requires_restart = false;       ///< Editor marks it; the value still applies to the next run
	bool hidden = false;                 ///< Persisted and scriptable, but not shown
};

/// @brief One registered setting. Held by the registry; `Setting<T>` is a handle to it
struct Entry {
	std::string key;
	Type type = Type::boolean;
	Meta meta;
	Value default_value;
	std::optional<Value> overrides[k_layer_count];    ///< Indexed by Layer minus one
	Value resolved;
	std::vector<std::function<void(const Value&)>> on_change;
};

/**
 * @brief The engine-wide settings registry
 *
 * Systems declare their own settings next to the code that consumes them and keep the returned handle;
 * nothing central has to know the list. Values arriving from disk before their declaration are held and
 * applied when it happens, so load order does not matter - the renderer declares its settings when it is
 * constructed, long after the files were read
 *
 * @note Every accessor is mutex-guarded, because the editor sets values from its UI thread while the engine
 * reads them on the main thread. Systems that need a value per frame should cache it through onChange()
 * rather than calling get() in a loop
 */
class TOAST_API Settings {
public:
	static auto get() -> Settings&;

	auto declareBool(std::string_view key, bool default_value, Meta meta = {}) -> Entry*;
	auto declareInt(std::string_view key, int64_t default_value, Meta meta = {}) -> Entry*;
	auto declareFloat(std::string_view key, double default_value, Meta meta = {}) -> Entry*;
	auto declareString(std::string_view key, std::string default_value, Meta meta = {}) -> Entry*;

	/// @returns the entry for @p key, or nullptr when nothing declared it
	[[nodiscard]]
	auto find(std::string_view key) -> Entry*;

	/// @brief Every declared setting, in declaration order
	///
	/// Stable across further declarations - entries live in a deque, so the editor may hold these across a
	/// frame in which another system registers
	[[nodiscard]]
	auto entries() -> std::vector<Entry*>;

	/// @returns the resolved value of @p key, or nullopt when it was never declared
	[[nodiscard]]
	auto value(std::string_view key) -> std::optional<Value>;

	/// @returns @p entry's resolved value; the lookup-free path Setting<T>::value() takes
	[[nodiscard]]
	auto resolved(const Entry* entry) -> Value;

	/// @brief Writes @p value into the active layer and fires the setting's change callbacks
	///
	/// A value of the wrong alternative is converted when the conversion is lossless-ish (int to double, bool
	/// to int) and rejected otherwise, so a caller cannot silently turn a float setting into a string
	auto setValue(std::string_view key, const Value& value) -> bool;

	/// @brief Drops @p key's override in the active layer, exposing whatever is underneath
	auto reset(std::string_view key) -> bool;

	/// @brief reset() for every declared setting
	void resetAll();

	/// @brief Points the two writable layers at their files; neither has to exist yet
	void setPaths(std::filesystem::path project_file, std::filesystem::path user_file);

	/// @brief Which layer setValue() writes to
	///
	/// The editor sets `project`, so what it authors ships as the game's defaults. A packaged game leaves it
	/// at `user`, so a player's tweaks land in their own file and survive a patch
	void setActiveLayer(Layer layer);

	[[nodiscard]]
	auto activeLayer() const -> Layer {
		return m_active_layer;
	}

	/// @returns the file the active layer writes to
	[[nodiscard]]
	auto activeFilePath() const -> const std::filesystem::path& {
		return m_active_layer == Layer::project ? m_project_path : m_user_path;
	}

	/// @brief Reads both writable layers from disk, replacing whatever they currently hold
	///
	/// Values whose key has not been declared yet are kept aside rather than dropped, and applied by the
	/// declare() that eventually names them
	void load();

	/// @brief Writes the active layer, creating parent directories as needed
	///
	/// Only keys that actually differ from the layer below are written, which is what keeps a user file to
	/// the handful of things that were changed - and lets a later patch move a default the player never
	/// touched
	auto save() -> bool;

	[[nodiscard]]
	auto dirty() const -> bool {
		return m_dirty;
	}

	/// @brief save(), but only when something changed since the last one
	auto saveIfDirty() -> bool;

private:
	auto declare(std::string_view key, Type type, Value default_value, Meta meta) -> Entry*;
	void resolveEntry(Entry& entry);
	auto loadFile(const std::filesystem::path& path, Layer layer) -> bool;

	/// @brief Lets the map be searched by string_view, without building a std::string to throw away
	struct TransparentHash {
		using is_transparent = void;

		auto operator()(std::string_view key) const -> size_t { return std::hash<std::string_view> {}(key); }
	};

	std::recursive_mutex m_mutex;
	std::deque<Entry> m_entries;
	std::unordered_map<std::string, Entry*, TransparentHash, std::equal_to<>> m_by_key;

	/// @brief Values read from disk whose key nothing has declared yet
	///
	/// A settings file outlives the code that reads it: a key may belong to a system not constructed yet, or
	/// to one that no longer exists. Holding them here means the first case works and the second is harmless
	/// - and save() writes them back out, so a file is never silently pruned by a run that did not touch it
	std::unordered_map<std::string, Value> m_pending[k_layer_count];

	std::filesystem::path m_project_path;
	std::filesystem::path m_user_path;
	Layer m_active_layer = Layer::user;
	bool m_dirty = false;
};

/**
 * @brief Typed handle to a declared setting
 *
 * Cheap to copy and stable for the process - entries are never removed. Reading is `value()`, writing is
 * `set()`, and a write persists on the next Settings::save()
 */
template<typename T>
class Setting {
public:
	Setting() = default;

	explicit Setting(Entry* entry) : m_entry(entry) { }

	[[nodiscard]]
	auto valid() const -> bool {
		return m_entry != nullptr;
	}

	[[nodiscard]]
	auto value() const -> T {
		if (m_entry == nullptr) {
			return T {};
		}
		// Reads the entry directly. Going back through value(key) hashed the key and built a std::string to
		// look up the entry this handle is already holding
		const Value resolved = Settings::get().resolved(m_entry);
		if (const auto* typed = std::get_if<T>(&resolved)) {
			return *typed;
		}
		return T {};
	}

	operator T() const {    // NOLINT(google-explicit-constructor) - the whole point is reading it like a T
		return value();
	}

	void set(T v) {
		if (m_entry != nullptr) {
			Settings::get().setValue(m_entry->key, Value {std::move(v)});
		}
	}

	/// @brief Runs @p callback whenever this setting changes, and once immediately with the current value
	///
	/// The immediate call is what lets a system express "apply this value" once and have both the initial
	/// state and every later change go through the same path
	void onChange(std::function<void(T)> callback) {
		if (m_entry == nullptr) {
			return;
		}
		auto wrapper = [callback](const Value& v) {
			if (const auto* typed = std::get_if<T>(&v)) {
				callback(*typed);
			}
		};
		wrapper(Value {value()});
		m_entry->on_change.emplace_back(std::move(wrapper));
	}

	[[nodiscard]]
	auto entry() const -> Entry* {
		return m_entry;
	}

private:
	Entry* m_entry = nullptr;
};

/// @brief declare* wrappers returning a typed handle, which is what call sites actually want
inline auto declareBool(std::string_view key, bool default_value, Meta meta = {}) -> Setting<bool> {
	return Setting<bool> {Settings::get().declareBool(key, default_value, std::move(meta))};
}

inline auto declareInt(std::string_view key, int64_t default_value, Meta meta = {}) -> Setting<int64_t> {
	return Setting<int64_t> {Settings::get().declareInt(key, default_value, std::move(meta))};
}

inline auto declareFloat(std::string_view key, double default_value, Meta meta = {}) -> Setting<double> {
	return Setting<double> {Settings::get().declareFloat(key, default_value, std::move(meta))};
}

inline auto declareString(std::string_view key, std::string default_value, Meta meta = {}) -> Setting<std::string> {
	return Setting<std::string> {Settings::get().declareString(key, std::move(default_value), std::move(meta))};
}

}
