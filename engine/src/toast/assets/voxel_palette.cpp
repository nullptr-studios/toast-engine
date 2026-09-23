#include "voxel_palette.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/physics/assets.hpp>
#include <toast/uid.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

namespace assets {

using voxel::Palette;
using voxel::PaletteEntry;

VoxelPalette::VoxelPalette(Palette palette, VoxelMaterialSlots slots, std::vector<uint8_t> defaulted)
    : m_palette(palette),
      m_slots(std::move(slots)),
      m_defaulted(std::move(defaulted)) {
	std::sort(m_defaulted.begin(), m_defaulted.end());
}

auto VoxelPalette::materialLibrary() const -> const voxel::MaterialLibrary& {
	m_library.materials.assign(k_palette_material_slots, voxel::PhysicalMaterial {});

	for (size_t i = 0; i < k_palette_material_slots; ++i) {
		const VoxelMaterialSlot& slot = m_slots[i];
		voxel::PhysicalMaterial& out = m_library.materials[i];

		if (slot.physics_uid != 0) {
			if (m_physics[i].uid().data() != slot.physics_uid) {
				m_physics[i] = load<PhysicsMaterial>(toast::UID(slot.physics_uid));
			}
			if (m_physics[i].hasValue()) {
				out.static_friction = m_physics[i]->staticFriction();
				out.dynamic_friction = m_physics[i]->dynamicFriction();
				out.restitution = m_physics[i]->restitution();
			}
		}

		if (slot.destruction_uid != 0) {
			if (m_destruction[i].uid().data() != slot.destruction_uid) {
				m_destruction[i] = load<DestructionMaterial>(toast::UID(slot.destruction_uid));
			}
			if (m_destruction[i].hasValue()) {
				const DestructionMaterial& d = *m_destruction[i];
				const double density = std::clamp(static_cast<double>(d.density()), 1.0, 65535.0);
				out.density = static_cast<uint16_t>(std::lround(density));
				out.toughness = d.toughness();
				out.structural_strength = d.structuralStrength();
				out.shatter_radius = d.shatterRadius();
				out.burn_rate = d.burnRate();
				if (d.flammable()) {
					out.flags |= voxel::k_material_flammable;
				}
			}
		}

		if (out.density == 0) {
			out.density = 1000;
		}
	}

	return m_library;
}

auto VoxelPalette::fromToml(const toml::table& table) -> std::unique_ptr<VoxelPalette> {
	ZoneScoped;
	Parsed parsed = parseToml(table);
	return std::make_unique<VoxelPalette>(parsed.palette, std::move(parsed.slots), std::move(parsed.defaulted));
}

void VoxelPalette::reload(const toml::table& table) {
	ZoneScoped;
	Parsed parsed = parseToml(table);

	const uint32_t revision = m_palette.revision + 1;
	m_palette = parsed.palette;
	m_palette.revision = revision;
	m_slots = std::move(parsed.slots);
	m_defaulted = std::move(parsed.defaulted);
	std::sort(m_defaulted.begin(), m_defaulted.end());

	m_physics.fill({});
	m_destruction.fill({});
}

auto VoxelPalette::parseToml(const toml::table& table) -> Parsed {
	ZoneScoped;

	Palette palette;

	if (const toml::node* scale = table.get("max_emissive")) {
		const std::optional<double> value = scale->is_number() ? scale->value<double>() : std::nullopt;
		if (!value || !(*value >= 0.0) || !std::isfinite(*value)) {
			throw std::runtime_error("voxel palette: max_emissive must be a non-negative number");
		}
		palette.max_emissive = static_cast<float>(*value);
	}

	VoxelMaterialSlots slots;
	if (const toml::array* materials = table["materials"].as_array()) {
		for (size_t i = 0; i < materials->size(); ++i) {
			if (i >= k_palette_material_slots) {
				TOAST_WARN(
				    "AssetManager",
				    "Voxel palette: {} material slots authored but only {} are addressable - the rest are dropped",
				    materials->size(),
				    k_palette_material_slots
				);
				break;
			}
			const toml::table* entry = (*materials)[i].as_table();
			if (entry == nullptr) {
				throw std::runtime_error("voxel palette: material slot " + std::to_string(i) + " is not a table");
			}

			const auto slot_uid = [&](std::string_view key) -> uint64_t {
				const toml::node* node = entry->get(key);
				if (node == nullptr) {
					return 0;
				}
				const std::optional<std::string> text = node->value<std::string>();
				const uint64_t uid = text ? toast::UID::fromString(*text) : 0;
				if (uid == 0) {
					throw std::runtime_error(
					    "voxel palette: material slot " + std::to_string(i) + ": " + std::string(key) + " is not a valid asset UID"
					);
				}
				return uid;
			};

			VoxelMaterialSlot& slot = slots[i];
			slot.name = entry->get("name") != nullptr ? entry->get("name")->value_or(std::string {}) : std::string {};
			slot.physics_uid = slot_uid("physics");
			slot.destruction_uid = slot_uid("destruction");
			slot.impact_sound = entry->get("impact_sound") != nullptr ? slot_uid("impact_sound") : 0;
			slot.tag = entry->get("tag") != nullptr ? entry->get("tag")->value_or(std::string {}) : std::string {};

			if (const toml::array* dust = entry->get("dust_colour") != nullptr ? entry->get("dust_colour")->as_array() : nullptr) {
				for (size_t c = 0; c < 3 && c < dust->size(); ++c) {
					slot.dust_colour[c] = static_cast<uint8_t>(std::clamp<int64_t>((*dust)[c].value_or<int64_t>(128), 0, 255));
				}
			}
		}
	}

	std::vector<uint8_t> defaulted;
	std::array<bool, voxel::k_palette_size> seen {};

	if (const toml::array* list = table["entries"].as_array()) {
		for (size_t i = 0; i < list->size(); ++i) {
			const toml::table* entry = (*list)[i].as_table();
			const auto fail = [&](const std::string& what) {
				return std::runtime_error("voxel palette: entry " + std::to_string(i) + ": " + what);
			};
			if (entry == nullptr) {
				throw fail("is not a table");
			}

			// Lambdas since file scope helpers could collide in the unity build
			const auto whole = [&](std::string_view key, int64_t low, int64_t high) -> std::optional<int64_t> {
				const toml::node* node = entry->get(key);
				if (node == nullptr) {
					return std::nullopt;
				}
				const std::optional<int64_t> value = node->is_integer() ? node->value<int64_t>() : std::nullopt;
				if (!value || *value < low || *value > high) {
					throw fail(std::string(key) + " must be a whole number " + std::to_string(low) + "-" + std::to_string(high));
				}
				return value;
			};
			const auto unit = [&](std::string_view key) -> uint8_t {
				const toml::node* node = entry->get(key);
				if (node == nullptr) {
					return 0;
				}
				const std::optional<double> value = node->is_number() ? node->value<double>() : std::nullopt;
				if (!value || !(*value >= 0.0 && *value <= 1.0)) {
					throw fail(std::string(key) + " must be a number from 0 to 1");
				}
				return static_cast<uint8_t>(std::lround(*value * 255.0));
			};

			const std::optional<int64_t> index = whole("index", 0, 255);
			if (!index) {
				throw fail("needs an index");
			}
			if (std::cmp_equal(*index, voxel::k_empty_palette_index)) {
				throw fail("index 0 is the empty voxel and cannot be authored");
			}
			if (seen[static_cast<size_t>(*index)]) {
				throw fail("index " + std::to_string(*index) + " appears twice");
			}
			seen[static_cast<size_t>(*index)] = true;

			PaletteEntry out;
			if (const toml::node* albedo = entry->get("albedo")) {
				const toml::array* rgb = albedo->as_array();
				if (rgb == nullptr || rgb->size() != 3) {
					throw fail("albedo must be three whole numbers 0-255");
				}
				std::array<uint8_t, 3> channels {};
				for (size_t c = 0; c < 3; ++c) {
					const std::optional<int64_t> channel = (*rgb)[c].is_integer() ? (*rgb)[c].value<int64_t>() : std::nullopt;
					if (!channel || *channel < 0 || *channel > 255) {
						throw fail("albedo must be three whole numbers 0-255");
					}
					channels[c] = static_cast<uint8_t>(*channel);
				}
				out.albedo_r = channels[0];
				out.albedo_g = channels[1];
				out.albedo_b = channels[2];
			}

			const auto unit_or = [&](std::string_view key, uint8_t fallback) -> uint8_t {
				return entry->get(key) != nullptr ? unit(key) : fallback;
			};

			out.roughness = unit("roughness");
			out.metallic = unit("metallic");
			out.reflectivity = unit("reflectivity");
			out.emissive = unit("emissive");

			out.alpha = unit_or("alpha", 255);

			if (const std::optional<int64_t> material = whole("material", 0, 255)) {
				out.material = static_cast<uint8_t>(*material);
			} else {
				out.material = voxel::k_default_material;
				defaulted.push_back(static_cast<uint8_t>(*index));
			}

			if (const std::optional<int64_t> becomes = whole("transforms_to", 0, 255)) {
				out.transforms_to = static_cast<uint8_t>(*becomes);
			}

			if (const toml::node* transparent = entry->get("transparent")) {
				const std::optional<bool> value = transparent->value_exact<bool>();
				if (!value) {
					throw fail("transparent must be true or false");
				}
				if (*value) {
					out.flags |= voxel::k_entry_transparent;
				}
			}

			palette.entries[static_cast<size_t>(*index)] = out;
		}
	}

	if (!defaulted.empty()) {
		std::string list;
		for (size_t i = 0; i < defaulted.size() && i < 8; ++i) {
			list += (i == 0 ? "" : ", ") + std::to_string(defaulted[i]);
		}
		if (defaulted.size() > 8) {
			list += ", ...";
		}
		TOAST_WARN("AssetManager", "Voxel palette: {} entries have no material and use the default: {}", defaulted.size(), list);
	}

	return Parsed {palette, slots, defaulted};
}

auto VoxelPalette::serialize(SaveMode /*mode*/) const -> std::vector<uint8_t> {
	ZoneScoped;

	toml::table root;
	root.insert("max_emissive", static_cast<double>(m_palette.max_emissive));

	toml::array materials;
	for (const VoxelMaterialSlot& slot : m_slots) {
		toml::table entry;
		entry.insert("name", slot.name);
		if (slot.physics_uid != 0) {
			entry.insert("physics", toast::UID::toString(slot.physics_uid));
		}
		if (slot.destruction_uid != 0) {
			entry.insert("destruction", toast::UID::toString(slot.destruction_uid));
		}
		if (slot.impact_sound != 0) {
			entry.insert("impact_sound", toast::UID::toString(slot.impact_sound));
		}
		if (!slot.tag.empty()) {
			entry.insert("tag", slot.tag);
		}
		entry.insert(
		    "dust_colour",
		    toml::array {
		      static_cast<int64_t>(slot.dust_colour[0]),
		      static_cast<int64_t>(slot.dust_colour[1]),
		      static_cast<int64_t>(slot.dust_colour[2])
		    }
		);
		materials.push_back(std::move(entry));
	}
	root.insert("materials", std::move(materials));

	const auto as_unit = [](uint8_t byte) { return static_cast<double>(byte) / 255.0; };

	toml::array list;
	for (uint32_t index = 1; index < voxel::k_palette_size; ++index) {
		const PaletteEntry& e = m_palette.entries[index];
		const bool is_defaulted = std::binary_search(m_defaulted.begin(), m_defaulted.end(), static_cast<uint8_t>(index));

		// Entries authored without a material are still written
		if (e == PaletteEntry {} && !is_defaulted) {
			continue;
		}

		toml::table entry;
		entry.insert("index", static_cast<int64_t>(index));
		entry.insert(
		    "albedo",
		    toml::array {static_cast<int64_t>(e.albedo_r), static_cast<int64_t>(e.albedo_g), static_cast<int64_t>(e.albedo_b)}
		);
		entry.insert("roughness", as_unit(e.roughness));
		entry.insert("metallic", as_unit(e.metallic));
		entry.insert("reflectivity", as_unit(e.reflectivity));
		entry.insert("emissive", as_unit(e.emissive));
		entry.insert("alpha", as_unit(e.alpha));
		if (!is_defaulted) {
			entry.insert("material", static_cast<int64_t>(e.material));
		}
		entry.insert("transforms_to", static_cast<int64_t>(e.transforms_to));
		entry.insert("transparent", (e.flags & voxel::k_entry_transparent) != 0);
		list.push_back(std::move(entry));
	}
	root.insert("entries", std::move(list));

	std::ostringstream stream;
	stream << root;
	const std::string text = stream.str();
	return {text.begin(), text.end()};
}

}
