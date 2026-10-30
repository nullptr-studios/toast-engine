/**
 * @file shader_registers.cpp
 * @author dario
 * @date 27/10/2026
 */

// Driver statistics of one pipeline built from SPIR-V
// usage shader_registers compute.spv | shader_registers fragment.spv vertex.spv
#include <vulkan/vulkan.h>
#include "spirv_reflect.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#define CHECK(x)                                                                 \
	do {                                                                         \
		VkResult r_ = (x);                                                       \
		if (r_ != VK_SUCCESS) {                                                  \
			std::fprintf(stderr, "%s failed %d at line %d\n", #x, r_, __LINE__); \
			return 1;                                                            \
		}                                                                        \
	} while (0)

static std::vector<uint32_t> load(const char* path) {
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) {
		std::fprintf(stderr, "cannot open %s\n", path);
		std::exit(1);
	}
	size_t size = f.tellg();
	std::vector<uint32_t> words(size / 4);
	f.seekg(0);
	f.read(reinterpret_cast<char*>(words.data()), size);
	return words;
}

struct Binding {
	VkDescriptorType type;
	uint32_t count;
};

struct Reflected {
	SpvReflectShaderModule module {};
	VkShaderStageFlagBits stage {};
};

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: shader_registers main.spv [vertex.spv]\n");
		return 1;
	}
	std::vector<std::vector<uint32_t>> code;
	std::vector<Reflected> modules;
	for (int i = 1; i < argc; ++i) {
		code.push_back(load(argv[i]));
	}
	for (auto& c : code) {
		Reflected r;
		if (spvReflectCreateShaderModule(c.size() * 4, c.data(), &r.module) != SPV_REFLECT_RESULT_SUCCESS) {
			std::fprintf(stderr, "reflect failed\n");
			return 1;
		}
		r.stage = static_cast<VkShaderStageFlagBits>(r.module.shader_stage);
		modules.push_back(r);
	}

	std::map<uint32_t, std::map<uint32_t, Binding>> sets;
	uint32_t push_size = 0;
	for (auto& m : modules) {
		uint32_t count = 0;
		spvReflectEnumerateDescriptorBindings(&m.module, &count, nullptr);
		std::vector<SpvReflectDescriptorBinding*> bindings(count);
		spvReflectEnumerateDescriptorBindings(&m.module, &count, bindings.data());
		for (auto* b : bindings) {
			uint32_t n = 1;
			for (uint32_t d = 0; d < b->array.dims_count; ++d) {
				n *= b->array.dims[d] == 0 ? 1 : b->array.dims[d];
			}
			if (b->array.dims_count > 0 && b->array.dims[0] == 0) {
				n = 64;
			}
			sets[b->set][b->binding] = Binding {static_cast<VkDescriptorType>(b->descriptor_type), n};
		}
		spvReflectEnumeratePushConstantBlocks(&m.module, &count, nullptr);
		std::vector<SpvReflectBlockVariable*> blocks(count);
		spvReflectEnumeratePushConstantBlocks(&m.module, &count, blocks.data());
		for (auto* b : blocks) {
			push_size = std::max(push_size, b->offset + b->size);
		}
	}

	VkApplicationInfo app {VK_STRUCTURE_TYPE_APPLICATION_INFO};
	app.apiVersion = VK_API_VERSION_1_4;
	VkInstanceCreateInfo ici {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	ici.pApplicationInfo = &app;
	VkInstance instance;
	CHECK(vkCreateInstance(&ici, nullptr, &instance));
	uint32_t gpu_count = 0;
	vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr);
	std::vector<VkPhysicalDevice> gpus(gpu_count);
	vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data());
	VkPhysicalDevice gpu = gpus[0];

	VkPhysicalDeviceMeshShaderFeaturesEXT mesh {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
	VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR exec {
	  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR
	};
	exec.pNext = &mesh;
	VkPhysicalDeviceRayQueryFeaturesKHR rq {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
	rq.pNext = &exec;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
	as.pNext = &rq;
	VkPhysicalDeviceVulkan14Features v14 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
	v14.pNext = &as;
	VkPhysicalDeviceVulkan13Features v13 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
	v13.pNext = &v14;
	VkPhysicalDeviceVulkan12Features v12 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
	v12.pNext = &v13;
	VkPhysicalDeviceVulkan11Features v11 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
	v11.pNext = &v12;
	VkPhysicalDeviceFeatures2 f2 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
	f2.pNext = &v11;
	vkGetPhysicalDeviceFeatures2(gpu, &f2);
	// The engine runs without robustness and bounds checks change the codegen
	f2.features.robustBufferAccess = VK_FALSE;
	v13.robustImageAccess = VK_FALSE;
	v14.pipelineRobustness = VK_FALSE;
	as.accelerationStructureCaptureReplay = VK_FALSE;
	as.accelerationStructureIndirectBuild = VK_FALSE;
	as.accelerationStructureHostCommands = VK_FALSE;
	mesh.multiviewMeshShader = VK_FALSE;
	mesh.primitiveFragmentShadingRateMeshShader = VK_FALSE;

	const char* extensions[] = {
	  "VK_KHR_pipeline_executable_properties", "VK_KHR_ray_query", "VK_KHR_acceleration_structure",
	  "VK_KHR_deferred_host_operations", "VK_EXT_mesh_shader",
	};
	float priority = 1.0f;
	VkDeviceQueueCreateInfo qci {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	qci.queueCount = 1;
	qci.pQueuePriorities = &priority;
	VkDeviceCreateInfo dci {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	dci.pNext = &f2;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	dci.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);
	dci.ppEnabledExtensionNames = extensions;
	VkDevice device;
	CHECK(vkCreateDevice(gpu, &dci, nullptr, &device));

	std::vector<VkDescriptorSetLayout> layouts;
	const uint32_t set_count = sets.empty() ? 0 : sets.rbegin()->first + 1;
	for (uint32_t s = 0; s < set_count; ++s) {
		std::vector<VkDescriptorSetLayoutBinding> lb;
		for (auto& [index, b] : sets[s]) {
			lb.push_back({index, b.type, b.count, VK_SHADER_STAGE_ALL, nullptr});
		}
		VkDescriptorSetLayoutCreateInfo lci {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		lci.bindingCount = static_cast<uint32_t>(lb.size());
		lci.pBindings = lb.data();
		VkDescriptorSetLayout l;
		CHECK(vkCreateDescriptorSetLayout(device, &lci, nullptr, &l));
		layouts.push_back(l);
	}
	VkPushConstantRange pcr {VK_SHADER_STAGE_ALL, 0, push_size};
	VkPipelineLayoutCreateInfo plci {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
	plci.pSetLayouts = layouts.data();
	plci.pushConstantRangeCount = push_size > 0 ? 1 : 0;
	plci.pPushConstantRanges = &pcr;
	VkPipelineLayout layout;
	CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &layout));

	std::vector<VkShaderModule> shader_modules;
	std::vector<VkPipelineShaderStageCreateInfo> stages;
	for (size_t i = 0; i < code.size(); ++i) {
		VkShaderModuleCreateInfo smci {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		smci.codeSize = code[i].size() * 4;
		smci.pCode = code[i].data();
		VkShaderModule sm;
		CHECK(vkCreateShaderModule(device, &smci, nullptr, &sm));
		shader_modules.push_back(sm);
		VkPipelineShaderStageCreateInfo st {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		st.stage = modules[i].stage;
		st.module = sm;
		st.pName = modules[i].module.entry_point_name;
		stages.push_back(st);
	}

	VkPipeline pipeline;
	if (modules[0].stage == VK_SHADER_STAGE_COMPUTE_BIT) {
		VkComputePipelineCreateInfo cpci {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
		cpci.flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
		cpci.stage = stages[0];
		cpci.layout = layout;
		CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline));
	} else {
		std::vector<VkFormat> colors;
		bool depth_out = false;
		for (auto& m : modules) {
			if (m.stage != VK_SHADER_STAGE_FRAGMENT_BIT) {
				continue;
			}
			uint32_t count = 0;
			spvReflectEnumerateOutputVariables(&m.module, &count, nullptr);
			std::vector<SpvReflectInterfaceVariable*> outputs(count);
			spvReflectEnumerateOutputVariables(&m.module, &count, outputs.data());
			std::map<uint32_t, VkFormat> by_location;
			for (auto* o : outputs) {
				if (o->built_in == SpvBuiltInFragDepth) {
					depth_out = true;
					continue;
				}
				if (o->built_in != -1 && (o->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)) {
					continue;
				}
				const bool is_uint = (o->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_INT) &&
				                     o->numeric.scalar.signedness == 0;
				const bool is_int = (o->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_INT) && !is_uint;
				by_location[o->location] =
				    is_uint ? VK_FORMAT_R32G32B32A32_UINT : (is_int ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R16G16B16A16_SFLOAT);
			}
			for (auto& [loc, fmt] : by_location) {
				colors.resize(std::max<size_t>(colors.size(), loc + 1), VK_FORMAT_R16G16B16A16_SFLOAT);
				colors[loc] = fmt;
			}
		}
		VkPipelineRenderingCreateInfo rci {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
		rci.colorAttachmentCount = static_cast<uint32_t>(colors.size());
		rci.pColorAttachmentFormats = colors.data();
		rci.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
		VkPipelineVertexInputStateCreateInfo vi {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		VkPipelineInputAssemblyStateCreateInfo ia {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkPipelineViewportStateCreateInfo vp {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vp.viewportCount = 1;
		vp.scissorCount = 1;
		VkPipelineRasterizationStateCreateInfo rs {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.lineWidth = 1.0f;
		VkPipelineMultisampleStateCreateInfo ms {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkPipelineDepthStencilStateCreateInfo ds {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = depth_out;
		ds.depthWriteEnable = depth_out;
		ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		std::vector<VkPipelineColorBlendAttachmentState> blend(colors.size());
		for (auto& b : blend) {
			b.colorWriteMask = 0xF;
		}
		VkPipelineColorBlendStateCreateInfo cb {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount = static_cast<uint32_t>(blend.size());
		cb.pAttachments = blend.data();
		VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dsi {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
		dsi.dynamicStateCount = 2;
		dsi.pDynamicStates = dyn;
		VkGraphicsPipelineCreateInfo gpci {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gpci.pNext = &rci;
		gpci.flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
		gpci.stageCount = static_cast<uint32_t>(stages.size());
		gpci.pStages = stages.data();
		gpci.pVertexInputState = &vi;
		gpci.pInputAssemblyState = &ia;
		gpci.pViewportState = &vp;
		gpci.pRasterizationState = &rs;
		gpci.pMultisampleState = &ms;
		gpci.pDepthStencilState = &ds;
		gpci.pColorBlendState = &cb;
		gpci.pDynamicState = &dsi;
		gpci.layout = layout;
		CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline));
	}

	auto get_props = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(
	    vkGetDeviceProcAddr(device, "vkGetPipelineExecutablePropertiesKHR")
	);
	auto get_stats = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(
	    vkGetDeviceProcAddr(device, "vkGetPipelineExecutableStatisticsKHR")
	);
	VkPipelineInfoKHR pinfo {VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
	pinfo.pipeline = pipeline;
	uint32_t exec_count = 0;
	get_props(device, &pinfo, &exec_count, nullptr);
	std::vector<VkPipelineExecutablePropertiesKHR> props(exec_count, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR});
	get_props(device, &pinfo, &exec_count, props.data());
	for (uint32_t e = 0; e < exec_count; ++e) {
		if (!(props[e].stages & (VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT))) {
			continue;
		}
		VkPipelineExecutableInfoKHR einfo {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
		einfo.pipeline = pipeline;
		einfo.executableIndex = e;
		uint32_t stat_count = 0;
		get_stats(device, &einfo, &stat_count, nullptr);
		std::vector<VkPipelineExecutableStatisticKHR> stats(stat_count, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
		get_stats(device, &einfo, &stat_count, stats.data());
		for (auto& s : stats) {
			std::printf("%s|", s.name);
			switch (s.format) {
				case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR: std::printf("%u\n", s.value.b32); break;
				case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR: std::printf("%lld\n", (long long)s.value.i64); break;
				case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR: std::printf("%llu\n", (unsigned long long)s.value.u64); break;
				case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR: std::printf("%f\n", s.value.f64); break;
				default: std::printf("?\n");
			}
		}
	}
	return 0;
}
