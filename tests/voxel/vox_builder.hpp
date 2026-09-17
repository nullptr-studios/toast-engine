/// @file vox_builder.hpp

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace voxbuild {

struct Builder {
	std::vector<uint8_t> body;

	/// Model of @p x by @p y by @p z holding palette index 1 at the origin plus an RGBA chunk
	[[nodiscard]]
	static auto singleVoxel(int32_t x, int32_t y, int32_t z) -> Builder {
		Builder builder;
		builder.size(x, y, z);
		builder.xyzi({{0, 0, 0, 1}});
		builder.rgba();
		return builder;
	}

	void i32(int32_t value) {
		std::array<uint8_t, 4> bytes {};
		std::memcpy(bytes.data(), &value, 4);
		body.insert(body.end(), bytes.begin(), bytes.end());
	}

	void u8(uint8_t value) { body.push_back(value); }

	void tag(const char* name) { body.insert(body.end(), name, name + 4); }

	void text(const std::string& value) {
		i32(static_cast<int32_t>(value.size()));
		body.insert(body.end(), value.begin(), value.end());
	}

	void dict(const std::vector<std::pair<std::string, std::string>>& entries) {
		i32(static_cast<int32_t>(entries.size()));
		for (const auto& entry : entries) {
			text(entry.first);
			text(entry.second);
		}
	}

	void chunk(const char* name, size_t start) {
		const std::vector<uint8_t> content(body.begin() + static_cast<std::ptrdiff_t>(start), body.end());
		body.resize(start);
		tag(name);
		i32(static_cast<int32_t>(content.size()));
		i32(0);
		body.insert(body.end(), content.begin(), content.end());
	}

	void size(int32_t x, int32_t y, int32_t z) {
		const size_t start = body.size();
		i32(x);
		i32(y);
		i32(z);
		chunk("SIZE", start);
	}

	void xyzi(const std::vector<std::array<uint8_t, 4>>& voxels) {
		const size_t start = body.size();
		i32(static_cast<int32_t>(voxels.size()));
		for (const std::array<uint8_t, 4>& voxel : voxels) {
			for (uint8_t component : voxel) {
				u8(component);
			}
		}
		chunk("XYZI", start);
	}

	void rgba() {
		const size_t start = body.size();
		for (uint32_t i = 0; i < 256; ++i) {
			u8(static_cast<uint8_t>(i));
			u8(static_cast<uint8_t>(255 - i));
			u8(7);
			u8(255);
		}
		chunk("RGBA", start);
	}

	void matl(int32_t index, const std::vector<std::pair<std::string, std::string>>& attributes) {
		const size_t start = body.size();
		i32(index);
		dict(attributes);
		chunk("MATL", start);
	}

	void ntrn(
	    int32_t id, int32_t child, const std::vector<std::pair<std::string, std::string>>& attributes,
	    const std::vector<std::pair<std::string, std::string>>& frame
	) {
		const size_t start = body.size();
		i32(id);
		dict(attributes);
		i32(child);
		i32(-1);
		i32(0);
		i32(1);
		dict(frame);
		chunk("nTRN", start);
	}

	void ngrp(int32_t id, const std::vector<int32_t>& children) {
		const size_t start = body.size();
		i32(id);
		dict({});
		i32(static_cast<int32_t>(children.size()));
		for (int32_t child : children) {
			i32(child);
		}
		chunk("nGRP", start);
	}

	void nshp(int32_t id, int32_t model) {
		const size_t start = body.size();
		i32(id);
		dict({});
		i32(1);
		i32(model);
		dict({});
		chunk("nSHP", start);
	}

	[[nodiscard]]
	auto file(int32_t version = 150) const -> std::vector<uint8_t> {
		Builder out;
		out.tag("VOX ");
		out.i32(version);
		out.tag("MAIN");
		out.i32(0);
		out.i32(static_cast<int32_t>(body.size()));
		out.body.insert(out.body.end(), body.begin(), body.end());
		return out.body;
	}
};

}
