/// @file vulkan_resource_base.hpp
/// @author dario
/// @date 6/27/2026.

#pragma once

#include "vulkan_common.hpp"
#include "vulkan_core.hpp"

namespace toast::renderer {

class IVulkanResource {
public:
	enum class UploadState {
		Uploading,
		Ready
	};

	virtual ~IVulkanResource() = default;

	void markUploading() { m_state = UploadState::Uploading; }

	void markReady() { m_state = UploadState::Ready; }

	bool isReady() const { return m_state == UploadState::Ready; }

private:
	UploadState m_state = UploadState::Uploading;
};

class PendingResourceUpload {
public:
	virtual ~PendingResourceUpload() = default;

	virtual IVulkanResource* resource() = 0;

	virtual void build(const VulkanCore& core) = 0;

	virtual void record(vk::CommandBuffer cmd) = 0;

	virtual void finished() { resource()->markReady(); }

	vk::raii::Fence completionFence = nullptr;
};

}
