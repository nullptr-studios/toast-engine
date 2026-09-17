#include "voxel_model.hpp"

#include <algorithm>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <tracy/Tracy.hpp>
#include <utility>

namespace assets {

using toast::voxel::BrickEntry;
using toast::voxel::BrickPool;
using toast::voxel::BrickTag;
using toast::voxel::k_brick_material_bytes;
using toast::voxel::Volume;

VoxelModel::VoxelModel(const std::vector<uint8_t>& data) {
	ZoneScoped;
	using _detail::VoxelFileHeader;

	const auto fail = [](const std::string& what) { return std::runtime_error(".tvox: " + what); };

	if (data.size() < sizeof(VoxelFileHeader)) {
		throw fail("truncated header");
	}
	VoxelFileHeader header;
	std::memcpy(&header, data.data(), sizeof(VoxelFileHeader));
	if (header.magic != VoxelFileHeader {}.magic) {
		throw fail("not a .tvox file");
	}
	if (header.version != _detail::voxel_format_version) {
		throw fail(
		    "version " + std::to_string(header.version) + " is not supported (this build reads " +
		    std::to_string(_detail::voxel_format_version) + " only); reimport the source model"
		);
	}
	if (header.dim_x == 0 || header.dim_y == 0 || header.dim_z == 0) {
		throw fail("a volume must be at least one brick along every axis");
	}

	m_brick_dims = glm::uvec3(header.dim_x, header.dim_y, header.dim_z);
	const size_t slots = static_cast<size_t>(header.dim_x) * header.dim_y * header.dim_z;

	// Every section checks the remaining length before allocating
	size_t offset = sizeof(VoxelFileHeader);
	const auto require = [&](size_t bytes) {
		if (data.size() - offset < bytes) {
			throw fail("truncated body");
		}
	};

	require(sizeof(uint64_t) + sizeof(uint32_t));
	std::memcpy(&m_palette_uid, data.data() + offset, sizeof(uint64_t));
	offset += sizeof(uint64_t);
	uint32_t stored = 0;
	std::memcpy(&stored, data.data() + offset, sizeof(uint32_t));
	offset += sizeof(uint32_t);

	require(slots * sizeof(BrickEntry));
	m_grid.resize(slots);
	std::memcpy(m_grid.data(), data.data() + offset, slots * sizeof(BrickEntry));
	offset += slots * sizeof(BrickEntry);

	const size_t brick_bytes = static_cast<size_t>(stored) * k_brick_material_bytes;
	require(brick_bytes);
	m_bricks.assign(
	    data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + brick_bytes)
	);
	offset += brick_bytes;

	if (offset != data.size()) {
		throw fail("trailing bytes after the last brick");
	}

	uint32_t next = 0;
	for (size_t i = 0; i < slots; ++i) {
		const BrickEntry entry = m_grid[i];
		const std::string where = "slot " + std::to_string(i) + ": ";
		switch (entry.tag()) {
			case BrickTag::empty:
				if (entry.payload() != 0) {
					throw fail(where + "an empty entry carries no payload");
				}
				break;
			case BrickTag::uniform:
				if (entry.payload() == 0 || entry.payload() > 255) {
					throw fail(where + "a uniform entry needs a material from 1 to 255");
				}
				break;
			case BrickTag::owned:
				if (entry.payload() != next) {
					throw fail(where + "stored bricks must each be referenced once, in file order");
				}
				++next;
				break;
			default: throw fail(where + "shared entries are a runtime state and cannot appear in a file");
		}
	}
	if (next < stored) {
		throw fail(std::to_string(stored - next) + " stored bricks are never referenced");
	}
	if (next > stored) {
		throw fail("the grid references more bricks than the file stores");
	}

	for (uint32_t b = 0; b < stored; ++b) {
		const uint8_t* begin = m_bricks.data() + (static_cast<size_t>(b) * k_brick_material_bytes);
		const uint8_t first = begin[0];
		if (std::all_of(begin, begin + k_brick_material_bytes, [first](uint8_t value) { return value == first; })) {
			throw fail("brick " + std::to_string(b) + " holds a single material and must be stored as an empty or uniform entry");
		}
	}
}

auto VoxelModel::capture(const Volume& volume, uint64_t palette_uid) -> std::unique_ptr<VoxelModel> {
	ZoneScoped;

	const glm::uvec3 dims = volume.brickDims();
	if (dims.x == 0 || dims.y == 0 || dims.z == 0 || dims.x > 0xFFFFu || dims.y > 0xFFFFu || dims.z > 0xFFFFu) {
		throw std::runtime_error(".tvox: a volume must be 1 to 65535 bricks along every axis to be saved");
	}

	std::unique_ptr<VoxelModel> out(new VoxelModel());
	out->m_brick_dims = dims;
	out->m_palette_uid = palette_uid;
	out->m_grid.reserve(volume.brickCount());

	uint32_t stored = 0;
	for (int32_t z = 0; std::cmp_less(z, dims.z); ++z) {
		for (int32_t y = 0; std::cmp_less(y, dims.y); ++y) {
			for (int32_t x = 0; std::cmp_less(x, dims.x); ++x) {
				const BrickEntry entry = volume.entryAt(glm::ivec3(x, y, z));
				if (!entry.isPooled()) {
					out->m_grid.push_back(entry);
					continue;
				}

				const std::span<const uint8_t, k_brick_material_bytes> bytes = volume.pool()->material(entry.payload());
				const uint8_t first = bytes[0];
				if (std::all_of(bytes.begin(), bytes.end(), [first](uint8_t value) { return value == first; })) {
					out->m_grid.push_back(first == 0 ? BrickEntry {} : BrickEntry::make(BrickTag::uniform, first));
					continue;
				}

				out->m_grid.push_back(BrickEntry::make(BrickTag::owned, stored++));
				out->m_bricks.insert(out->m_bricks.end(), bytes.begin(), bytes.end());
			}
		}
	}
	return out;
}

auto VoxelModel::instantiate(BrickPool& pool) const -> std::optional<Volume> {
	ZoneScoped;

	Volume volume(pool, m_brick_dims);
	size_t slot = 0;
	for (int32_t z = 0; std::cmp_less(z, m_brick_dims.z); ++z) {
		for (int32_t y = 0; std::cmp_less(y, m_brick_dims.y); ++y) {
			for (int32_t x = 0; std::cmp_less(x, m_brick_dims.x); ++x, ++slot) {
				const glm::ivec3 brick(x, y, z);
				const BrickEntry entry = m_grid[slot];
				switch (entry.tag()) {
					case BrickTag::uniform: volume.setBrickUniform(brick, static_cast<uint8_t>(entry.payload())); break;
					case BrickTag::owned: {
						const std::span<const uint8_t, k_brick_material_bytes> bytes(
						    m_bricks.data() + (static_cast<size_t>(entry.payload()) * k_brick_material_bytes), k_brick_material_bytes
						);
						if (!volume.setBrickMaterial(brick, bytes)) {
							return std::nullopt;
						}
						break;
					}
					default: break;
				}
			}
		}
	}
	return {std::move(volume)};
}

auto VoxelModel::solidVoxelCount() const -> uint32_t {
	uint32_t total = 0;
	for (const BrickEntry& entry : m_grid) {
		if (entry.tag() == BrickTag::uniform) {
			total += toast::voxel::k_brick_voxel_count;
		}
	}
	for (uint8_t value : m_bricks) {
		total += value != toast::voxel::k_empty_palette_index ? 1u : 0u;
	}
	return total;
}

auto VoxelModel::serialize(SaveMode /*mode*/) const -> std::vector<uint8_t> {
	ZoneScoped;

	_detail::VoxelFileHeader header;
	header.dim_x = static_cast<uint16_t>(m_brick_dims.x);
	header.dim_y = static_cast<uint16_t>(m_brick_dims.y);
	header.dim_z = static_cast<uint16_t>(m_brick_dims.z);
	const uint32_t stored = storedBrickCount();

	std::vector<uint8_t> out(
	    sizeof(header) + sizeof(m_palette_uid) + sizeof(stored) + (m_grid.size() * sizeof(BrickEntry)) + m_bricks.size()
	);
	size_t offset = 0;
	const auto put = [&](const void* source, size_t bytes) {
		if (bytes != 0) {
			std::memcpy(out.data() + offset, source, bytes);
			offset += bytes;
		}
	};
	put(&header, sizeof(header));
	put(&m_palette_uid, sizeof(m_palette_uid));
	put(&stored, sizeof(stored));
	put(m_grid.data(), m_grid.size() * sizeof(BrickEntry));
	put(m_bricks.data(), m_bricks.size());
	return out;
}

}
