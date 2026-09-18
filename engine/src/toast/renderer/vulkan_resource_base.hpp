/// @file vulkan_resource_base.hpp
/// @author dario
/// @date 6/27/2026

#pragma once

#include "vulkan_common.hpp"
#include "vulkan_core.hpp"

namespace renderer {

class IVulkanResource {
public:
	enum class UploadState : std::uint8_t {
		uploading,
		ready,
		failed_load,
		failed_gpu
	};

	virtual ~IVulkanResource() = default;

	void markUploading() { m_state = UploadState::uploading; }

	void markReady() { m_state = UploadState::ready; }

	void markFailed(UploadState reason) { m_state = reason; }

	[[nodiscard]]
	auto isReady() const -> bool {
		return m_state == UploadState::ready;
	}

	[[nodiscard]]
	auto hasFailed() const -> bool {
		return m_state == UploadState::failed_load || m_state == UploadState::failed_gpu;
	}

	[[nodiscard]]
	auto state() const -> UploadState {
		return m_state;
	}

private:
	UploadState m_state = UploadState::uploading;
};

class PendingResourceUpload {
public:
	virtual ~PendingResourceUpload() = default;

	virtual auto resource() -> IVulkanResource* = 0;

	virtual void build(const VulkanCore& core) = 0;

	virtual void record(vk::CommandBuffer cmd) = 0;

	virtual void finished() {
		if (!resource()->hasFailed()) {
			resource()->markReady();
		}
	}

	vk::DeviceSize host_bytes = 0;
};

}
