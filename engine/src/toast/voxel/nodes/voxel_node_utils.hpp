/**
 * @file voxel_node_utils.hpp
 * @author Xein
 * @date 27 Sep 2026
 * @brief Helpers the voxel nodes share, not part of the API
 */

#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/voxel/assets/voxel_model.hpp>
#include <toast/voxel/procedural_pool.hpp>
#include <toast/voxel/voxel_volume.hpp>

namespace toast::_detail {

/** Inspector message ids the procedural pieces use */
enum : uint8_t {
	k_message_outside = 40,
	k_message_misaligned = 41,
};

/** The loaded asset when it is really of type, null otherwise */
template<typename T>
[[nodiscard]]
auto assetOfType(const assets::Handle<T>& handle, std::string_view type) -> const T* {
	if (!handle.hasValue()) {
		return nullptr;
	}
	const assets::Asset& asset = static_cast<const assets::HandleBase&>(handle).get();
	return asset.type() == type ? static_cast<const T*>(&asset) : nullptr;
}

/** Loads a model into the procedural pool */
[[nodiscard]]
inline auto instantiateModel(const assets::VoxelModel& model) -> std::unique_ptr<voxel::Volume> {
	std::optional<voxel::Volume> instance = model.instantiate(voxel::proceduralBrickPool());
	if (!instance.has_value()) {
		return nullptr;
	}
	return std::make_unique<voxel::Volume>(std::move(*instance));
}

}
