#include "voxel_node.hpp"

#include <toast/log.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/voxel/runtime_pool.hpp>

namespace toast {

namespace {

/// A UID shared by two assets resolves to the wrong one so check through the untyped base
template<typename T>
[[nodiscard]]
auto voxelNodeAssetOfType(const assets::Handle<T>& handle, std::string_view type) -> const T* {
	if (!handle.hasValue()) {
		return nullptr;
	}
	const assets::Asset& asset = static_cast<const assets::HandleBase&>(handle).get();
	return asset.type() == type ? static_cast<const T*>(&asset) : nullptr;
}

}

auto VoxelNode::paletteUid() const -> uint64_t {
	if (m_palette.uid().data() != 0) {
		return m_palette.uid().data();
	}
	if (const auto* model = voxelNodeAssetOfType(m_model, "voxel_model")) {
		return model->paletteUid();
	}
	return 0;
}

auto VoxelNode::localBoundingSphere() const -> glm::vec4 {
	const auto* model = voxelNodeAssetOfType(m_model, "voxel_model");
	if (model == nullptr) {
		return glm::vec4(0.0f);
	}
	const glm::vec3 extent = glm::vec3(model->brickDims()) * voxel::k_brick_size;
	return {extent * 0.5f, glm::length(extent) * 0.5f};
}

auto VoxelNode::latticePlacement() const -> std::optional<voxel::LatticePlacement> {
	return voxel::placementFromTransform(getWorldTransform());
}

auto VoxelNode::volume() -> voxel::Volume* {
	const assets::VoxelModel* model = voxelNodeAssetOfType(m_model, "voxel_model");

	if (m_model.hasValue() && model == nullptr && m_reported_wrong_model != m_model.uid().data()) {
		m_reported_wrong_model = m_model.uid().data();
		TOAST_WARN(
		    "Voxel",
		    "'{}' names model {}, which resolves to a '{}' asset, not a voxel model - two assets share that UID. Reimport the "
		    "source to give them separate ones",
		    name(),
		    m_model.uid().get(),
		    static_cast<const assets::HandleBase&>(m_model).get().type()
		);
	}

	if (model != m_instanced_from) {
		releaseVolume();
		m_instanced_from = model;
		m_model_palette = {};

		if (model != nullptr) {
			m_volume = model->instantiate(voxel::runtimeBrickPool());
			if (!m_volume.has_value()) {
				TOAST_WARN(
				    "Voxel",
				    "'{}' could not be instantiated: the runtime brick pool is out of its {} bricks",
				    name(),
				    voxel::k_runtime_brick_capacity
				);
			}
		}
		++m_revision;
	}
	return m_volume.has_value() ? &*m_volume : nullptr;
}

auto VoxelNode::resolvedPalette() -> const voxel::Palette* {
	if (const auto* palette = voxelNodeAssetOfType(m_palette, "voxel_palette")) {
		return &palette->palette();
	}

	if (m_palette.hasValue() && m_reported_wrong_palette != m_palette.uid().data()) {
		m_reported_wrong_palette = m_palette.uid().data();
		TOAST_WARN(
		    "Voxel",
		    "'{}' names palette {}, which resolves to a '{}' asset, not a voxel palette - two assets share that UID",
		    name(),
		    m_palette.uid().get(),
		    static_cast<const assets::HandleBase&>(m_palette).get().type()
		);
	}

	if (m_palette.uid().data() != 0) {
		return nullptr;
	}

	const auto* model = voxelNodeAssetOfType(m_model, "voxel_model");
	if (model == nullptr || model->paletteUid() == 0) {
		return nullptr;
	}
	if (m_model_palette.uid().data() != model->paletteUid()) {
		m_model_palette = assets::load<assets::VoxelPalette>(UID(model->paletteUid()));
	}
	const auto* palette = voxelNodeAssetOfType(m_model_palette, "voxel_palette");
	return palette != nullptr ? &palette->palette() : nullptr;
}

void VoxelNode::releaseVolume() {
	m_volume.reset();
	m_instanced_from = nullptr;
}

void VoxelNode::init() {
	m_registered_proxy = renderer::registerVoxelNodeProxy(this);
}

void VoxelNode::end() {
	releaseVolume();
	if (!m_registered_proxy) {
		return;
	}

	renderer::unregisterVoxelNodeProxy(this);
	m_registered_proxy = false;
}

void VoxelNode::destroy() {
	end();
}

}
