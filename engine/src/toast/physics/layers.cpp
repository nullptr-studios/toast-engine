#include "layers.hpp"

#include "physics_settings.hpp"

namespace physics {

namespace {

struct LayerInfo {
	std::string name;
	std::string abbreviation;
};

struct LayerTable {
	std::mutex mutex;
	std::array<LayerInfo, k_layer_count> layers;
};

auto table() -> LayerTable& {
	static LayerTable instance;
	return instance;
}

auto defaultLayer(int layer) -> LayerInfo {
	switch (layer) {
		case default_layer: return {.name = "Default", .abbreviation = "0"};
		case trigger_layer: return {.name = "Trigger", .abbreviation = "T"};
		case characters_layer: return {.name = "Characters", .abbreviation = "Ch"};
		case player_layer: return {.name = "Player", .abbreviation = "P"};
		case small_debris_layer: return {.name = "Small debris", .abbreviation = "Ds"};
		case large_debris_layer: return {.name = "Large debris", .abbreviation = "Dl"};
		case projectiles_layer: return {.name = "Projectiles", .abbreviation = "Pry"};
		default: return {.name = "Layer " + std::to_string(layer), .abbreviation = std::to_string(layer)};
	}
}

}

auto defaultLayerNames() -> std::vector<std::string> {
	std::vector<std::string> names;
	names.reserve(k_layer_count);
	for (int layer = 0; layer < k_layer_count; ++layer) {
		names.push_back(defaultLayer(layer).name);
	}
	return names;
}

auto defaultLayerAbbreviations() -> std::vector<std::string> {
	std::vector<std::string> abbreviations;
	abbreviations.reserve(k_layer_count);
	for (int layer = 0; layer < k_layer_count; ++layer) {
		abbreviations.push_back(defaultLayer(layer).abbreviation);
	}
	return abbreviations;
}

auto layerMask(const std::vector<int>& layers) -> uint32_t {
	if (layers.empty()) {
		return k_all_layers;
	}
	uint32_t mask = 0;
	for (const int layer : layers) {
		mask |= layerBit(layer);
	}
	return mask;
}

auto getLayer(std::string_view name) -> int {
	LayerTable& all = table();
	std::scoped_lock lock(all.mutex);
	for (int layer = 0; layer < k_layer_count; ++layer) {
		if (all.layers[layer].name == name) {
			return layer;
		}
	}
	return -1;
}

auto layerName(int layer) -> std::string {
	if (layer < 0 || layer >= k_layer_count) {
		return {};
	}
	LayerTable& all = table();
	std::scoped_lock lock(all.mutex);
	return all.layers[layer].name;
}

auto layerAbbreviation(int layer) -> std::string {
	if (layer < 0 || layer >= k_layer_count) {
		return {};
	}
	LayerTable& all = table();
	std::scoped_lock lock(all.mutex);
	return all.layers[layer].abbreviation;
}

void registerLayerSettings(PhysicsParams& params) {
	{
		LayerTable& all = table();
		std::scoped_lock lock(all.mutex);
		for (int layer = 0; layer < k_layer_count; ++layer) {
			all.layers[layer] = defaultLayer(layer);
		}
	}

	params.layer_names.onChange([](const std::vector<std::string>& names) {
		LayerTable& all = table();
		std::scoped_lock lock(all.mutex);
		for (int layer = 1; layer < k_layer_count; ++layer) {
			const auto index = static_cast<size_t>(layer);
			all.layers[layer].name = index < names.size() && !names[index].empty() ? names[index] : defaultLayer(layer).name;
		}
	});
	params.layer_abbreviations.onChange([](const std::vector<std::string>& abbreviations) {
		LayerTable& all = table();
		std::scoped_lock lock(all.mutex);
		for (int layer = 1; layer < k_layer_count; ++layer) {
			const auto index = static_cast<size_t>(layer);
			all.layers[layer].abbreviation = index < abbreviations.size() && !abbreviations[index].empty()
			                                     ? abbreviations[index].substr(0, k_layer_abbreviation_length)
			                                     : defaultLayer(layer).abbreviation;
		}
	});
}

}
