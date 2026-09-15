#include "input_action.hpp"
#include "input_layout.hpp"
#include "input_settings.hpp"

#include <algorithm>
#include <toast/uid.hpp>

namespace assets {

Action::Action(const toml::table& table, Handle<Schema> schema) : Data(table, std::move(schema), Data::keep_all_keys) {
	// Cache typed fields from m_root so getters can return lightweight string_views
	const auto& d = static_cast<const DataValue&>(m_root);

	if (d.contains("name")) {
		m_name = d["name"].as<std::string>();
	}
	if (d.contains("function_name")) {
		m_function_name = d["function_name"].as<std::string>();
	}
	if (d.contains("description")) {
		m_description = d["description"].as<std::string>();
	}

	std::string type_str;
	if (d.contains("type")) {
		type_str = d["type"].as<std::string>();
	}
	if (type_str == "Action1D") {
		m_value_type = ActionValueType::action_1d;
	} else if (type_str == "Action2D") {
		m_value_type = ActionValueType::action_2d;
	} else {
		m_value_type = ActionValueType::action_0d;
	}

	// accumulation = true selects Highest, false selects Average, matching the asset schema default
	bool acc = false;
	if (d.contains("accumulation")) {
		acc = d["accumulation"].as<bool>();
	}
	m_accumulation = acc ? AccumulationType::highest : AccumulationType::average;
}

auto Action::get() const -> toml::table {
	return m_root.asTomlTable();
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

InputLayout::InputLayout(const toml::table& table, Handle<Schema> schema) : Data(table, std::move(schema), Data::keep_all_keys) {
	const auto& d = static_cast<const DataValue&>(m_root);

	if (d.contains("name")) {
		m_name = d["name"].as<std::string>();
	}

	// Every layout owns a hidden default layer that is always present and listed first
	m_layers.emplace_back(default_layer);
	if (d.contains("layers") && d["layers"].isArray()) {
		const auto& layers_val = d["layers"];
		for (size_t i = 0; i < layers_val.size(); ++i) {
			const auto& elem = layers_val[i];
			if (elem.type() == DataType::string_t) {
				auto layer_name = elem.as<std::string>();
				if (layer_name != default_layer) {
					m_layers.push_back(std::move(layer_name));
				}
			}
		}
	}

	if (d.contains("action") && d["action"].isArray()) {
		const auto& actions_val = d["action"];
		for (size_t i = 0; i < actions_val.size(); ++i) {
			const auto& node = actions_val[i];
			if (!node.isObject()) {
				continue;
			}

			Entry entry;

			if (node.contains("id")) {
				const auto& id_val = node["id"];
				if (id_val.type() == DataType::string_t) {
					auto id_str = id_val.as<std::string>();
					if (id_str.size() == 11) {
						entry.id = toast::UID(toast::UID::fromString(id_str));
					}
				} else if (id_val.type() == DataType::int_t) {
					entry.id = toast::UID(static_cast<uint64_t>(id_val.as<int64_t>()));
				}
			}

			if (node.contains("included") && node["included"].isArray()) {
				const auto& inc = node["included"];
				for (size_t j = 0; j < inc.size(); ++j) {
					if (inc[j].type() == DataType::string_t) {
						entry.included.push_back(inc[j].as<std::string>());
					}
				}
			}

			if (node.contains("excluded") && node["excluded"].isArray()) {
				const auto& exc = node["excluded"];
				for (size_t j = 0; j < exc.size(); ++j) {
					if (exc[j].type() == DataType::string_t) {
						entry.excluded.push_back(exc[j].as<std::string>());
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

auto InputSettings::get() const -> toml::table {
	return m_root.asTomlTable();
}

auto InputSettings::getFloat(std::string_view key, float fallback) const noexcept -> float {
	const auto& d = m_root;
	if (!d.contains(key)) {
		return fallback;
	}
	const auto& v = d[key];
	if (v.type() == DataType::float_t) {
		return static_cast<float>(v.as<double>());
	}
	if (v.type() == DataType::int_t) {
		return static_cast<float>(v.as<int64_t>());
	}
	return fallback;
}

}
