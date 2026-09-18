#include "voxel_material_library.hpp"

#include <sstream>
#include <stdexcept>
#include <toast/log.hpp>
#include <toast/uid.hpp>
#include <tracy/Tracy.hpp>
#include <unordered_set>

namespace assets {

using voxel::MaterialLibrary;
using voxel::PhysicalMaterial;
using voxel::TableIssue;
using voxel::TableProblem;

VoxelMaterialLibrary::VoxelMaterialLibrary(MaterialLibrary library, std::vector<VoxelMaterialInfo> info)
    : m_library(std::move(library)),
      m_info(std::move(info)) {
	// Parallel tables so keep them the same length
	m_info.resize(m_library.materials.size());
}

auto VoxelMaterialLibrary::indexOf(std::string_view name) const -> std::optional<uint32_t> {
	for (uint32_t i = 0; i < m_info.size(); ++i) {
		if (m_info[i].name == name) {
			return i;
		}
	}
	return std::nullopt;
}

auto VoxelMaterialLibrary::fromToml(const toml::table& table) -> std::unique_ptr<VoxelMaterialLibrary> {
	ZoneScoped;

	const toml::array* list = table["materials"].as_array();
	if (list == nullptr) {
		throw std::runtime_error("voxel material library: no [[materials]] array");
	}

	MaterialLibrary library;
	std::vector<VoxelMaterialInfo> info;
	std::unordered_set<std::string> names;

	for (size_t i = 0; i < list->size(); ++i) {
		const toml::table* entry = (*list)[i].as_table();
		const auto fail = [&](const std::string& what) {
			return std::runtime_error("voxel material library: material " + std::to_string(i) + ": " + what);
		};
		if (entry == nullptr) {
			throw fail("is not a table");
		}

		// Lambdas since file scope helpers could collide in the unity build
		const auto number = [&](std::string_view key, float fallback) -> float {
			const toml::node* node = entry->get(key);
			if (node == nullptr) {
				return fallback;
			}
			const std::optional<double> value = node->is_number() ? node->value<double>() : std::nullopt;
			if (!value) {
				throw fail(std::string(key) + " must be a number");
			}
			return static_cast<float>(*value);
		};
		const auto flag = [&](std::string_view key) -> bool {
			const toml::node* node = entry->get(key);
			if (node == nullptr) {
				return false;
			}
			const std::optional<bool> value = node->value_exact<bool>();
			if (!value) {
				throw fail(std::string(key) + " must be true or false");
			}
			return *value;
		};

		VoxelMaterialInfo meta;
		const std::optional<std::string> name = (*entry)["name"].value<std::string>();
		if (!name || name->empty()) {
			throw fail("needs a name");
		}
		if (!names.insert(*name).second) {
			throw fail("duplicate name '" + *name + "'");
		}
		meta.name = *name;

		PhysicalMaterial material;
		const std::optional<int64_t> density = (*entry)["density"].is_integer() ? (*entry)["density"].value<int64_t>() : std::nullopt;
		if (!density || *density < 0 || *density > 65535) {
			throw fail("density must be a whole number of kg/m3 between 0 and 65535");
		}
		material.density = static_cast<uint16_t>(*density);

		material.toughness = number("toughness", material.toughness);
		material.structural_strength = number("structural_strength", material.structural_strength);
		material.shatter_radius = number("shatter_radius", material.shatter_radius);
		material.static_friction = number("static_friction", material.static_friction);
		material.dynamic_friction = number("dynamic_friction", material.dynamic_friction);
		material.restitution = number("restitution", material.restitution);
		material.ignition_energy = number("ignition_energy", material.ignition_energy);
		material.burn_rate = number("burn_rate", material.burn_rate);
		material.fuel = number("fuel", material.fuel);

		if (flag("indestructible")) {
			material.flags |= voxel::k_material_indestructible;
		}
		if (flag("passable")) {
			material.flags |= voxel::k_material_passable;
		}
		if (flag("flammable")) {
			material.flags |= voxel::k_material_flammable;
		}

		if (const toml::node* sound = entry->get("impact_sound")) {
			const std::optional<std::string> uid = sound->value<std::string>();
			meta.impact_sound = uid ? toast::UID::fromString(*uid) : 0;
			if (meta.impact_sound == 0) {
				throw fail("impact_sound is not a valid asset UID");
			}
		}

		if (const toml::node* colour = entry->get("dust_colour")) {
			const toml::array* rgb = colour->as_array();
			if (rgb == nullptr || rgb->size() != 3) {
				throw fail("dust_colour must be three whole numbers 0-255");
			}
			for (size_t c = 0; c < 3; ++c) {
				const std::optional<int64_t> channel = (*rgb)[c].is_integer() ? (*rgb)[c].value<int64_t>() : std::nullopt;
				if (!channel || *channel < 0 || *channel > 255) {
					throw fail("dust_colour must be three whole numbers 0-255");
				}
				meta.dust_colour[c] = static_cast<uint8_t>(*channel);
			}
		}

		if (const toml::node* tag = entry->get("tag")) {
			const std::optional<std::string> text = tag->value<std::string>();
			if (!text) {
				throw fail("tag must be a string");
			}
			meta.tag = *text;
		}

		library.materials.push_back(material);
		info.push_back(std::move(meta));
	}

	for (const TableProblem& problem : validateLibrary(library)) {
		const std::string who = problem.index < info.size() ? "'" + info[problem.index].name + "'" : std::string();
		switch (problem.issue) {
			case TableIssue::library_empty:
				TOAST_WARN("AssetManager", "Voxel material library has no materials; palettes have no default to fall back to");
				break;
			case TableIssue::library_too_large:
				TOAST_WARN(
				    "AssetManager",
				    "Voxel material library has {} materials; at most {} are supported",
				    problem.index,
				    voxel::k_max_physical_materials
				);
				break;
			case TableIssue::zero_density:
				TOAST_WARN("AssetManager", "Voxel material {} has zero density - it will have no mass", who);
				break;
			case TableIssue::out_of_range:
				TOAST_WARN("AssetManager", "Voxel material {} has a negative or NaN property, or restitution above 1", who);
				break;
			case TableIssue::indestructible_with_zero_toughness:
				TOAST_WARN("AssetManager", "Voxel material {} is indestructible but has zero toughness", who);
				break;
			default: break;
		}
	}

	return std::make_unique<VoxelMaterialLibrary>(std::move(library), std::move(info));
}

auto VoxelMaterialLibrary::serialize(SaveMode /*mode*/) const -> std::vector<uint8_t> {
	ZoneScoped;

	toml::array list;
	for (uint32_t i = 0; i < size(); ++i) {
		const PhysicalMaterial& material = m_library.materials[i];
		const VoxelMaterialInfo& meta = m_info[i];

		toml::table entry;
		entry.insert("name", meta.name);
		entry.insert("density", static_cast<int64_t>(material.density));
		entry.insert("toughness", static_cast<double>(material.toughness));
		entry.insert("structural_strength", static_cast<double>(material.structural_strength));
		entry.insert("shatter_radius", static_cast<double>(material.shatter_radius));
		entry.insert("static_friction", static_cast<double>(material.static_friction));
		entry.insert("dynamic_friction", static_cast<double>(material.dynamic_friction));
		entry.insert("restitution", static_cast<double>(material.restitution));
		entry.insert("ignition_energy", static_cast<double>(material.ignition_energy));
		entry.insert("burn_rate", static_cast<double>(material.burn_rate));
		entry.insert("fuel", static_cast<double>(material.fuel));
		entry.insert("indestructible", material.isIndestructible());
		entry.insert("passable", !material.collides());
		entry.insert("flammable", material.isFlammable());
		if (meta.impact_sound != 0) {
			entry.insert("impact_sound", toast::UID::toString(meta.impact_sound));
		}
		entry.insert(
		    "dust_colour",
		    toml::array {
		      static_cast<int64_t>(meta.dust_colour[0]),
		      static_cast<int64_t>(meta.dust_colour[1]),
		      static_cast<int64_t>(meta.dust_colour[2])
		    }
		);
		if (!meta.tag.empty()) {
			entry.insert("tag", meta.tag);
		}
		list.push_back(std::move(entry));
	}

	toml::table root;
	root.insert("materials", std::move(list));

	std::ostringstream stream;
	stream << root;
	const std::string text = stream.str();
	return {text.begin(), text.end()};
}

}
