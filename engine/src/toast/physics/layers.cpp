#include "layers.hpp"

#include <array>
#include <mutex>
#include <toast/settings/settings.hpp>

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

auto settingKey(int layer, std::string_view field) -> std::string {
	return "physics.layers." + std::to_string(layer) + "." + std::string {field};
}

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

void registerLayerSettings() {
	LayerTable& all = table();
	{
		std::scoped_lock lock(all.mutex);
		for (int layer = 0; layer < k_layer_count; ++layer) {
			all.layers[layer] = defaultLayer(layer);
		}
	}

	// Default can not be renamed so it has no setting
	for (int layer = 1; layer < k_layer_count; ++layer) {
		const LayerInfo info = defaultLayer(layer);
		toast::settings::declareString(settingKey(layer, "name"), info.name, {.label = info.name, .category = "Tags", .hidden = true})
		    .onChange([layer](const std::string& value) {
			    LayerTable& all = table();
			    std::scoped_lock lock(all.mutex);
			    all.layers[layer].name = value;
		    });
		toast::settings::declareString(
		    settingKey(layer, "abbr"), info.abbreviation, {.label = info.abbreviation, .category = "Tags", .hidden = true}
		)
		    .onChange([layer](const std::string& value) {
			    LayerTable& all = table();
			    std::scoped_lock lock(all.mutex);
			    all.layers[layer].abbreviation = value.substr(0, k_layer_abbreviation_length);
		    });
	}
}

}
