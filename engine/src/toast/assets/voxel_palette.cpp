#include "voxel_palette.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <toast/log.hpp>
#include <toast/uid.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

namespace assets {

using voxel::Palette;
using voxel::PaletteEntry;

VoxelPalette::VoxelPalette(Palette palette, uint64_t library_uid, std::vector<uint8_t> defaulted)
    : m_palette(palette),
      m_library_uid(library_uid),
      m_defaulted(std::move(defaulted)) {
	std::sort(m_defaulted.begin(), m_defaulted.end());
}

auto VoxelPalette::fromToml(const toml::table& table) -> std::unique_ptr<VoxelPalette> {
	ZoneScoped;

	Palette palette;

	if (const toml::node* scale = table.get("max_emissive")) {
		const std::optional<double> value = scale->is_number() ? scale->value<double>() : std::nullopt;
		if (!value || !(*value >= 0.0) || !std::isfinite(*value)) {
			throw std::runtime_error("voxel palette: max_emissive must be a non-negative number");
		}
		palette.max_emissive = static_cast<float>(*value);
	}

	uint64_t library_uid = 0;
	if (const toml::node* library = table.get("library")) {
		const std::optional<std::string> uid = library->value<std::string>();
		library_uid = uid ? toast::UID::fromString(*uid) : 0;
		if (library_uid == 0) {
			throw std::runtime_error("voxel palette: library is not a valid asset UID");
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

			out.roughness = unit("roughness");
			out.metallic = unit("metallic");
			out.reflectivity = unit("reflectivity");
			out.emissive = unit("emissive");

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

	return std::make_unique<VoxelPalette>(palette, library_uid, std::move(defaulted));
}

auto VoxelPalette::serialize(SaveMode /*mode*/) const -> std::vector<uint8_t> {
	ZoneScoped;

	toml::table root;
	if (m_library_uid != 0) {
		root.insert("library", toast::UID::toString(m_library_uid));
	}
	root.insert("max_emissive", static_cast<double>(m_palette.max_emissive));

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
