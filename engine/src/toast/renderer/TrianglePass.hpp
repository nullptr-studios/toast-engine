/// @file TrianglePass.hpp
/// @author dario
/// @date 07/06/2026.
///
#pragma once
#include "IRenderPass.hpp"
#include "ShaderLayout.hpp"
#include "VulkanPipeline.hpp"

namespace toast::renderer {
class VulkanCore;
}

// DEBUG TESTING PASS
class TrianglePass : public IRenderPass {
public:
	struct FrameUniformData {
		std::array<float, 2> iResolution;
		float iTime;
		float padding;
	};

	TrianglePass(const toast::renderer::VulkanCore& core, vk::Format colorFormat, vk::Format depthFormat, vk::Extent2D extent);

	void update(uint32_t frame_index, float dt) override;

	void record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) override;

private:
	void createResources(const toast::renderer::VulkanCore& core);
	auto updateFrameUniformData(uint32_t frame_index) -> void;

	float totalTime = 0.0f;

	toast::renderer::ShaderLayout m_shaderlayout;
	toast::renderer::VulkanPipeline m_pipeline;

	vk::raii::DescriptorPool m_descriptorPool = nullptr;
	std::vector<FrameUniformData> m_uniformsData;
	std::vector<FrameResources> m_uniformsRes;
};
