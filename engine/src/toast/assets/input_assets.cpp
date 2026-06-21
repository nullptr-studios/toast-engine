#include "input_action.hpp"
#include "input_layout.hpp"
#include "input_settings.hpp"

#include <algorithm>
#include <sstream>

namespace assets {

namespace {
/// @brief Serializes a TOML table to UTF-8 bytes, shared by every input asset
auto tableToBytes(const toml::table& table) -> std::vector<uint8_t> {
	std::ostringstream ss;
	ss << table;
	auto str = ss.str();
	return {str.begin(), str.end()};
}

/// @brief Reads a string node, returning a fallback when the node is missing
auto readString(const toml::table& table, std::string_view key, std::string_view fallback) -> std::string {
	if (auto value = table[key].value<std::string>()) {
		return *value;
	}
	return std::string(fallback);
}
}

Action::Action(toml::table table) : m_table(std::move(table)) {
	m_name = readString(m_table, "name", "unnamed");
	m_function_name = readString(m_table, "function_name", "onUnnamed");
	m_description = readString(m_table, "description", "");

	const std::string type = readString(m_table, "type", "Action0D");
	if (type == "Action1D") {
		m_value_type = ActionValueType::action_1d;
	} else if (type == "Action2D") {
		m_value_type = ActionValueType::action_2d;
	} else {
		m_value_type = ActionValueType::action_0d;
	}

	// accumulation = true selects Highest, false selects Average, matching the asset schema default
	m_accumulation = m_table["accumulation"].value_or(false) ? AccumulationType::highest : AccumulationType::average;
}

auto Action::get() const noexcept -> const toml::table& {
	return m_table;
}

auto Action::name() const noexcept -> std::string_view {
	return m_name;
}

auto Action::functionName() const noexcept -> std::string_view {
	return m_function_name;
}

auto Action::description() const noexcept -> std::string_view {
	return m_description;
}

auto Action::valueType() const noexcept -> ActionValueType {
	return m_value_type;
}

auto Action::accumulation() const noexcept -> AccumulationType {
	return m_accumulation;
}

auto Action::serialize(SaveMode) const -> std::vector<uint8_t> {
	return tableToBytes(m_table);
}

InputLayout::InputLayout(toml::table table) : m_table(std::move(table)) {
	m_name = readString(m_table, "name", "Unnamed Layout");

	// Every layout owns a hidden default layer that is always present and listed first
	m_layers.emplace_back(default_layer);
	if (const auto* layers = m_table["layers"].as_array()) {
		for (const auto& node : *layers) {
			if (auto name = node.value<std::string>(); name && *name != default_layer) {
				m_layers.push_back(*name);
			}
		}
	}

	if (const auto* actions = m_table["action"].as_array()) {
		for (const auto& node : *actions) {
			const auto* entry_table = node.as_table();
			if (!entry_table) {
				continue;
			}

			Entry entry;
			if (auto id = (*entry_table)["id"].value<std::string>()) {
				entry.id = toast::UID(toast::UID::fromString(*id));
			} else if (auto raw = (*entry_table)["id"].value<int64_t>()) {
				entry.id = toast::UID(static_cast<uint64_t>(*raw));
			}

			if (const auto* included = (*entry_table)["included"].as_array()) {
				for (const auto& layer : *included) {
					if (auto name = layer.value<std::string>()) {
						entry.included.push_back(*name);
					}
				}
			}
			if (const auto* excluded = (*entry_table)["excluded"].as_array()) {
				for (const auto& layer : *excluded) {
					if (auto name = layer.value<std::string>()) {
						entry.excluded.push_back(*name);
					}
				}
			}

			m_entries.push_back(std::move(entry));
		}
	}
}

auto InputLayout::name() const noexcept -> std::string_view {
	return m_name;
}

auto InputLayout::layers() const noexcept -> const std::vector<std::string>& {
	return m_layers;
}

auto InputLayout::entries() const noexcept -> const std::vector<Entry>& {
	return m_entries;
}

auto InputLayout::isActiveForLayer(const Entry& entry, std::string_view layer) noexcept -> bool {
	// An explicit blacklist always wins
	if (std::ranges::find(entry.excluded, layer) != entry.excluded.end()) {
		return false;
	}
	// An empty whitelist means the action is active on every layer
	if (entry.included.empty()) {
		return true;
	}
	return std::ranges::find(entry.included, layer) != entry.included.end();
}

auto InputLayout::serialize(SaveMode) const -> std::vector<uint8_t> {
	return tableToBytes(m_table);
}

auto InputSettings::get() const noexcept -> const toml::table& {
	return m_table;
}

auto InputSettings::getFloat(std::string_view key, float fallback) const noexcept -> float {
	return m_table[key].value_or(fallback);
}

auto InputSettings::serialize(SaveMode) const -> std::vector<uint8_t> {
	return tableToBytes(m_table);
}

}
