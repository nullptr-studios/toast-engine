#include "toast/renderer/resources/scene_descriptor_set.hpp"

#include "toast/renderer/core/descriptor_writer.hpp"
#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/passes/lighting/cluster_lighting_pass.hpp"
#include "toast/renderer/passes/lighting/environment_pass.hpp"
#include "toast/renderer/passes/lighting/gi_pass.hpp"
#include "toast/renderer/passes/lighting/reflection_probe_pass.hpp"
#include "toast/renderer/passes/lighting/shadow_pass.hpp"
#include "toast/renderer/raytracing/ray_tracing_scene.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <format>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

void SceneDescriptorSets::create(
    const VulkanCore& core, const ShaderReflection& reflection, vk::DescriptorSetLayout layout, std::string_view owner
) {
	ZoneScoped;
	m_core = &core;
	m_owner = owner;

	const auto& device = core.getDevice();
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const auto* cluster_lighting = VulkanRenderer::instance->getClusterLightingPass();

	m_sets.clear();
	m_sets.reserve(VulkanRenderer::k_frames_in_flight);
	m_scene_binding.reset();
	m_bound_tlas.assign(VulkanRenderer::k_frames_in_flight, {});

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_sets[i], std::format("{} SceneSet[{}]", m_owner, i));

		const auto* frame_res = VulkanRenderer::instance->getFrameUBORes(i);
		if (!frame_res->gpu_buffer.has_value()) {
			TOAST_CRITICAL("Render", "Frame UBO buffer missing for frame {}", i);
			continue;
		}

		DescriptorWriter writer;

		for (const auto& binding : reflection.bindings) {
			if (binding.set != 0) {
				continue;
			}

			const bool is_irradiance = binding.name == "gIrradianceMap";
			if (is_irradiance || binding.name == "gPrefilteredEnv") {
				const auto* environment = VulkanRenderer::instance->getEnvironmentPass();
				vk::ImageView view;
				if (environment != nullptr) {
					view = is_irradiance ? environment->getIrradianceView() : environment->getPrefilteredView();
				}

				if (!view && i == 0) {
					TOAST_WARN(
					    "Render", "'{}' bound the black fallback cubemap to {} - no environment lighting here", m_owner, binding.name
					);
				}

				writer.image(
				    *m_sets[i],
				    binding.binding,
				    vk::DescriptorType::eCombinedImageSampler,
				    environment != nullptr ? environment->getSampler() : VulkanRenderer::instance->getDefaultSampler(),
				    view ? view : VulkanRenderer::instance->getDefaultCubeView(),
				    vk::ImageLayout::eShaderReadOnlyOptimal
				);
				continue;
			}

			// Left unwritten for GiPass own sets since it is created after them and they never sample GI
			if (binding.name == "gGiIrradiance") {
				if (const auto* gi = VulkanRenderer::instance->giPass(); gi != nullptr) {
					writer.image(
					    *m_sets[i],
					    binding.binding,
					    vk::DescriptorType::eCombinedImageSampler,
					    gi->irradianceSampler(),
					    gi->irradianceView(),
					    vk::ImageLayout::eShaderReadOnlyOptimal
					);
				}
				continue;
			}

			const bool is_probe_irradiance = binding.name == "gProbeIrradiance";
			if (binding.name == "gProbeMaps" || is_probe_irradiance) {
				const auto* probes = VulkanRenderer::instance->getReflectionProbePass();
				const uint32_t count = std::max(binding.count, 1U);

				std::vector<vk::DescriptorImageInfo> probe_infos;
				probe_infos.reserve(count);
				for (uint32_t probe = 0; probe < count; ++probe) {
					vk::ImageView view;
					if (probes != nullptr) {
						view = is_probe_irradiance ? probes->getProbeIrradianceView(probe) : probes->getProbeView(probe);
					}

					probe_infos.emplace_back(
					    probes != nullptr ? probes->getSampler() : VulkanRenderer::instance->getDefaultSampler(),
					    view ? view : VulkanRenderer::instance->getDefaultCubeView(),
					    vk::ImageLayout::eShaderReadOnlyOptimal
					);
				}

				writer.imageArray(*m_sets[i], binding.binding, vk::DescriptorType::eCombinedImageSampler, probe_infos);
				continue;
			}

			if (binding.name == "gPunctualShadowDepths") {
				if (!*m_shadow_depth_sampler) {
					m_shadow_depth_sampler = vk::raii::Sampler(device, nearestClampSamplerInfo());
				}
				const auto* shadow_pass = VulkanRenderer::instance->getShadowPass();
				const vk::ImageView view = shadow_pass != nullptr ? shadow_pass->getPunctualMapView(i) : vk::ImageView {};
				writer.image(
				    *m_sets[i],
				    binding.binding,
				    vk::DescriptorType::eCombinedImageSampler,
				    *m_shadow_depth_sampler,
				    view ? view : VulkanRenderer::instance->getDefaultShadowMapView(),
				    vk::ImageLayout::eShaderReadOnlyOptimal
				);
				continue;
			}

			const bool is_cascade_map = binding.name == "gShadowMap";
			if (is_cascade_map || binding.name == "gPunctualShadowMaps") {
				const auto* shadow_pass = VulkanRenderer::instance->getShadowPass();
				vk::ImageView view;
				if (shadow_pass != nullptr) {
					view = is_cascade_map ? shadow_pass->getCascadeMapView(i) : shadow_pass->getPunctualMapView(i);
				}

				const bool has_map = static_cast<bool>(view);
				writer.image(
				    *m_sets[i],
				    binding.binding,
				    vk::DescriptorType::eCombinedImageSampler,
				    has_map && shadow_pass != nullptr ? shadow_pass->getShadowSampler()
						                                  : VulkanRenderer::instance->getDefaultShadowSampler(),
				    has_map ? view : VulkanRenderer::instance->getDefaultShadowMapView(),
				    vk::ImageLayout::eShaderReadOnlyOptimal
				);
				continue;
			}

			// Never written without ray query which is legal because the layout marks it partially bound
			if (binding.name == "gScene") {
				m_scene_binding = binding.binding;
				continue;
			}

			vk::Buffer buffer;
			vk::DeviceSize size = VK_WHOLE_SIZE;
			vk::DescriptorType descriptor_type = vk::DescriptorType::eUniformBuffer;

			if (binding.name == "gCamera") {
				buffer = **frame_res->gpu_buffer;
				size = sizeof(FrameUBO);
			} else if (cluster_lighting != nullptr && binding.name == "gClusterParams") {
				buffer = cluster_lighting->getClusterParamsBuffer(i);
			} else if (cluster_lighting != nullptr && binding.name == "gLights") {
				buffer = cluster_lighting->getLightsBuffer(i);
				descriptor_type = vk::DescriptorType::eStorageBuffer;
			} else if (cluster_lighting != nullptr && binding.name == "gClusterLightGrid") {
				buffer = cluster_lighting->getClusterLightGridBuffer(i);
				descriptor_type = vk::DescriptorType::eStorageBuffer;
			} else if (cluster_lighting != nullptr && binding.name == "gLightIndexList") {
				buffer = cluster_lighting->getLightIndexListBuffer(i);
				descriptor_type = vk::DescriptorType::eStorageBuffer;
			} else if (binding.name == "gJointMatrices") {
				buffer = VulkanRenderer::instance->getJointMatrixBuffer(i);
				size = sizeof(glm::mat4) * k_max_joint_matrices;
				descriptor_type = vk::DescriptorType::eStorageBuffer;
			} else if (binding.name == "gInstances") {
				buffer = VulkanRenderer::instance->getInstanceBuffer(i);
				size = sizeof(InstanceData) * k_max_instances;
				descriptor_type = vk::DescriptorType::eStorageBuffer;
			} else if (binding.name == "gFogVolumes") {
				buffer = VulkanRenderer::instance->getFogVolumeBuffer(i);
				descriptor_type = vk::DescriptorType::eStorageBuffer;
			} else if (binding.name == "gBlueNoise") {
				buffer = VulkanRenderer::instance->getBlueNoiseBuffer();
				descriptor_type = vk::DescriptorType::eStorageBuffer;
			} else if (binding.name.starts_with("gGi")) {
				if (const auto* gi = VulkanRenderer::instance->giPass(); gi != nullptr) {
					buffer = gi->sceneBuffer(binding.name);
				}
				descriptor_type = vk::DescriptorType::eStorageBuffer;
			} else if (binding.name == "gIrradianceSH") {
				if (auto* probes = VulkanRenderer::instance->getReflectionProbePassMutable(); probes != nullptr) {
					buffer = probes->getShBuffer();
				}
				descriptor_type = vk::DescriptorType::eStorageBuffer;
			} else {
				TOAST_WARN("Render", "'{}': unrecognized scene-set binding '{}', leaving unbound", m_owner, binding.name);
				continue;
			}

			writer.buffer(*m_sets[i], binding.binding, descriptor_type, buffer, size);
		}

		writer.flush(device);
	}
}

void SceneDescriptorSets::updateTlas(uint32_t frame_index) {
	ZoneScoped;
	if (m_core == nullptr || !m_scene_binding.has_value() || frame_index >= m_bound_tlas.size()) {
		return;
	}

	const auto* scene = VulkanRenderer::instance->getRayTracingScene();
	const vk::AccelerationStructureKHR tlas = scene != nullptr ? scene->getAccelerationStructure(frame_index) : nullptr;
	const std::pair bound {tlas, scene != nullptr ? scene->getAccelerationStructureGeneration(frame_index) : 0};

	// A descriptor cannot be written with a null structure
	if (!tlas || bound == m_bound_tlas[frame_index]) {
		return;
	}

	DescriptorWriter writer;
	writer.accelerationStructure(*m_sets[frame_index], *m_scene_binding, tlas);
	writer.flush(m_core->getDevice());

	m_bound_tlas[frame_index] = bound;
}

}
