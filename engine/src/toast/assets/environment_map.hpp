/**
 * @file environment_map.hpp
 * @author dario
 * @date 24/11/2026
 *
 * @brief Radiance .hdr sky and image-based lighting source, referenced by nodes through a handle
 */

#pragma once
#include "core_types.hpp"

namespace assets {

/// Holds no pixels: the environment pass decodes the file from Handle::path() when it switches maps
class TOAST_API EnvironmentMap : public Asset {
public:
	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "environment_map";
	}
};

}
