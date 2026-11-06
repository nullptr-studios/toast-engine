/// @file vulkan_renderer_detail.hpp
/// @author dario
/// @date 30/10/2026

#pragma once

#include "toast/renderer/render_frame.hpp"
#include "toast/renderer/resources/vulkan_mesh.hpp"

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <vulkan/vulkan.hpp>

namespace renderer::detail {

/// Single writer thread only so the add skips the locked read modify write
template<typename T>
inline void addSingleWriter(std::atomic<T>& counter, std::type_identity_t<T> value) noexcept {
	counter.store(counter.load(std::memory_order_relaxed) + value, std::memory_order_relaxed);
}

inline auto packExtent(vk::Extent2D extent) -> uint64_t {
	return (static_cast<uint64_t>(extent.width) << 32u) | static_cast<uint64_t>(extent.height);
}

inline auto unpackExtent(uint64_t packed) -> vk::Extent2D {
	return {static_cast<uint32_t>(packed >> 32u), static_cast<uint32_t>(packed & 0xFFFFFFFFu)};
}

inline auto isTraceable(const MeshInstanceProxy& proxy) -> bool {
	if (proxy.mesh == nullptr) {
		return false;
	}

	if (proxy.posed_vertex_offset != MeshInstanceProxy::k_no_posed_vertices) {
		return proxy.mesh->isReady() && proxy.mesh->getVertexCount() > 0 && proxy.mesh->getIndexCount() >= 3;
	}

	return proxy.mesh->hasAccelerationStructure();
}

void clearCachedAssetHandles();

}
