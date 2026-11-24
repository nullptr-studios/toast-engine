/**
 * @file dlss_upscaler.cpp
 * @author dario
 * @date 22/11/2026
 */

#include "toast/renderer/upscale/dlss_upscaler.hpp"

#include "toast/renderer/core/depth_convention.hpp"
#include "toast/renderer/core/vulkan_core.hpp"

#include <toast/logger.hpp>

#ifdef TOAST_DLSS
#include <SDL3/SDL_filesystem.h>
#include <nvsdk_ngx_helpers_vk.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace renderer {

#ifdef TOAST_DLSS

namespace {
/// GUID-like id NGX takes from engines without an NVIDIA application id
constexpr const char* k_project_id = "6b85a39b-e850-4545-982c-44e42ac0a548";
constexpr const char* k_engine_version = "1.0";
constexpr uint32_t k_vendor_nvidia = 0x10DE;

/// %AppData%/nullptr-studios/toast-engine/logs like the log server, NGX writes its own logs there
auto dataPath() -> const std::wstring& {
	// NGX takes wchar_t paths, std::filesystem::path::c_str() is char on Linux
	static const std::wstring path = [] {
		std::filesystem::path dir;
		std::error_code ec;
		if (char* pref_path = SDL_GetPrefPath("nullptr-studios", "toast-engine")) {
			dir = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(pref_path))) / "logs";
			SDL_free(pref_path);
		} else {
			dir = std::filesystem::temp_directory_path(ec) / "nullptr-studios" / "toast-engine" / "logs";
		}
		std::filesystem::create_directories(dir, ec);
		return dir.wstring();
	}();
	return path;
}

/// NGX itself only searches the exe directory and the editor exe lives elsewhere
auto engineDirectory() -> std::wstring {
#ifndef _WIN32
	Dl_info info {};
	if (dladdr(reinterpret_cast<void*>(&engineDirectory), &info) == 0 || info.dli_fname == nullptr) {
		return {};
	}
	return std::filesystem::path(info.dli_fname).parent_path().wstring();
#else
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(
	        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	        reinterpret_cast<LPCWSTR>(&engineDirectory),
	        &module
	    )) {
		return {};
	}
	std::array<wchar_t, MAX_PATH> path {};
	const DWORD length = GetModuleFileNameW(module, path.data(), MAX_PATH);
	if (length == 0 || length >= MAX_PATH) {
		return {};
	}
	return std::filesystem::path(path.data()).parent_path().wstring();
#endif
}

auto discoveryInfo() -> NVSDK_NGX_FeatureDiscoveryInfo {
	NVSDK_NGX_FeatureDiscoveryInfo info {};
	info.SDKVersion = NVSDK_NGX_Version_API;
	info.FeatureID = NVSDK_NGX_Feature_SuperSampling;
	info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
	info.Identifier.v.ProjectDesc = {k_project_id, NVSDK_NGX_ENGINE_TYPE_CUSTOM, k_engine_version};
	info.ApplicationDataPath = dataPath().c_str();    // same wide string, see dataPath()
	return info;
}

auto extensionNames(const VkExtensionProperties* properties, uint32_t count) -> std::vector<const char*> {
	std::vector<const char*> names;
	for (uint32_t i = 0; i < count; ++i) {
		names.push_back(properties[i].extensionName);
	}
	return names;
}

auto resultString(NVSDK_NGX_Result result) -> std::string {
	auto text = std::format("0x{:08X}", static_cast<uint32_t>(result));
	if (const wchar_t* name = GetNGXResultAsString(result)) {
		text += ' ';
		// NGX result names are ASCII
		for (; *name != L'\0'; ++name) {
			text += static_cast<char>(*name);
		}
	}
	return text;
}

/// Transformer model, NVIDIA's default for DLAA, Quality and Balanced
constexpr auto k_render_preset = NVSDK_NGX_DLSS_Hint_Render_Preset_K;

/// Nearest NGX quality mode for the render scale since the slider is continuous
auto qualityFor(vk::Extent2D render_extent, vk::Extent2D output_extent) -> NVSDK_NGX_PerfQuality_Value {
	const float scale = static_cast<float>(render_extent.width) / static_cast<float>(output_extent.width);
	if (scale >= 0.99f) {
		return NVSDK_NGX_PerfQuality_Value_DLAA;
	}
	if (scale >= 0.62f) {
		return NVSDK_NGX_PerfQuality_Value_MaxQuality;
	}
	return scale >= 0.54f ? NVSDK_NGX_PerfQuality_Value_Balanced : NVSDK_NGX_PerfQuality_Value_MaxPerf;
}

auto imageResource(vk::ImageView view, vk::Image image, vk::Format format, vk::Extent2D extent, bool depth, bool write)
    -> NVSDK_NGX_Resource_VK {
	const vk::ImageSubresourceRange range(depth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
	return NVSDK_NGX_Create_ImageView_Resource_VK(
	    view, image, range, static_cast<VkFormat>(format), extent.width, extent.height, write
	);
}
}    // namespace

auto DlssUpscaler::instanceExtensions() -> std::vector<const char*> {
	const auto info = discoveryInfo();
	uint32_t count = 0;
	VkExtensionProperties* properties = nullptr;
	if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info, &count, &properties))) {
		return {};
	}
	return extensionNames(properties, count);
}

auto DlssUpscaler::deviceExtensions(vk::Instance instance, vk::PhysicalDevice physical_device) -> std::vector<const char*> {
	const auto info = discoveryInfo();
	uint32_t count = 0;
	VkExtensionProperties* properties = nullptr;
	if (NVSDK_NGX_FAILED(
	        NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, physical_device, &info, &count, &properties)
	    )) {
		return {};
	}
	return extensionNames(properties, count);
}

DlssUpscaler::DlssUpscaler(const VulkanCore& core) : m_core(&core) {
	ZoneScoped;
	m_failure_reason = core.dlssExtensionsEnabled() ? initialize() : std::string(core.dlssReason());
	m_available = m_failure_reason.empty();
	const auto props = core.getPhysicalDevice().getProperties();
	if (m_available) {
		TOAST_INFO("Render", "DLSS ready (Super Resolution on {})", props.deviceName.data());
		return;
	}
	release();
	if (props.vendorID == k_vendor_nvidia) {
		TOAST_WARN("Render", "DLSS unavailable on {}: {}", props.deviceName.data(), m_failure_reason);
	} else {
		TOAST_INFO("Render", "DLSS unavailable: {}", m_failure_reason);
	}
}

auto DlssUpscaler::initialize() -> std::string {
	const auto& instance = m_core->getInstance();
	const auto& device = m_core->getDevice();

	const std::wstring engine_dir = engineDirectory();
	const std::array<const wchar_t*, 1> search_paths {engine_dir.c_str()};
	NVSDK_NGX_FeatureCommonInfo common_info {};
	if (!engine_dir.empty()) {
		common_info.PathListInfo.Path = search_paths.data();
		common_info.PathListInfo.Length = static_cast<unsigned int>(search_paths.size());
	}

	auto result = NVSDK_NGX_VULKAN_Init_with_ProjectID(
	    k_project_id,
	    NVSDK_NGX_ENGINE_TYPE_CUSTOM,
	    k_engine_version,
	    dataPath().c_str(),
	    *instance,
	    *m_core->getPhysicalDevice(),
	    *device,
	    instance.getDispatcher()->vkGetInstanceProcAddr,
	    device.getDispatcher()->vkGetDeviceProcAddr,
	    &common_info
	);
	if (NVSDK_NGX_FAILED(result)) {
		return std::format("NGX initialisation failed ({})", resultString(result));
	}
	m_ngx_initialized = true;

	result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&m_parameters);
	if (NVSDK_NGX_FAILED(result) || m_parameters == nullptr) {
		m_parameters = nullptr;
		return std::format("NGX capability query failed ({})", resultString(result));
	}

	int needs_driver = 0;
	if (NVSDK_NGX_SUCCEED(m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver)) &&
	    needs_driver != 0) {
		unsigned int major = 0;
		unsigned int minor = 0;
		m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &major);
		m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minor);
		return std::format("the NVIDIA driver is too old, DLSS needs {}.{} or newer", major, minor);
	}

	int available = 0;
	if (NVSDK_NGX_FAILED(m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available)) || available == 0) {
		int init_result = NVSDK_NGX_Result_Fail;
		m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &init_result);
		return std::format("Super Resolution is not available ({})", resultString(static_cast<NVSDK_NGX_Result>(init_result)));
	}
	return {};
}

void DlssUpscaler::ensureFeature(vk::CommandBuffer cmd, vk::Extent2D render_extent, vk::Extent2D output_extent) {
	if (!m_available || m_create_failed) {
		return;
	}
	if (m_feature != nullptr) {
		if ((render_extent != m_feature_render || output_extent != m_feature_output) && !m_mismatch_logged) {
			m_mismatch_logged = true;
			setRuntimeReason("the frame extents changed without a resize");
			TOAST_WARN(
			    "Render",
			    "DLSS feature is {}x{} -> {}x{} but the frame is {}x{} -> {}x{}, the extents changed without TaaPass::onResize; "
			    "using TAA until the next resize",
			    m_feature_render.width,
			    m_feature_render.height,
			    m_feature_output.width,
			    m_feature_output.height,
			    render_extent.width,
			    render_extent.height,
			    output_extent.width,
			    output_extent.height
			);
		}
		return;
	}

	ZoneScoped;
	for (const char* hint :
	     {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
	      NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
	      NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
	      NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
	      NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
	      NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality}) {
		NVSDK_NGX_Parameter_SetUI(m_parameters, hint, k_render_preset);
	}

	int flags =
	    NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
	if constexpr (depth::k_reversed_z) {
		flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
	}

	NVSDK_NGX_DLSS_Create_Params create {};
	create.Feature.InWidth = render_extent.width;
	create.Feature.InHeight = render_extent.height;
	create.Feature.InTargetWidth = output_extent.width;
	create.Feature.InTargetHeight = output_extent.height;
	create.Feature.InPerfQualityValue = qualityFor(render_extent, output_extent);
	create.InFeatureCreateFlags = flags;

	const auto result = NGX_VULKAN_CREATE_DLSS_EXT1(*m_core->getDevice(), cmd, 1, 1, &m_feature, m_parameters, &create);
	if (NVSDK_NGX_FAILED(result) || m_feature == nullptr) {
		m_feature = nullptr;
		m_create_failed = true;
		setRuntimeReason(std::format("feature creation failed ({})", resultString(result)));
		TOAST_WARN(
		    "Render",
		    "DLSS feature creation failed for {}x{} -> {}x{} ({}); using TAA until the next resize",
		    render_extent.width,
		    render_extent.height,
		    output_extent.width,
		    output_extent.height,
		    resultString(result)
		);
		return;
	}
	m_feature_render = render_extent;
	m_feature_output = output_extent;
	m_reset_pending = true;
	m_mismatch_logged = false;
	m_evaluate_failure_logged = false;
	TOAST_INFO(
	    "Render",
	    "DLSS feature created ({}x{} -> {}x{}, quality {}, preset K)",
	    render_extent.width,
	    render_extent.height,
	    output_extent.width,
	    output_extent.height,
	    static_cast<int>(create.Feature.InPerfQualityValue)
	);
}

auto DlssUpscaler::evaluate(vk::CommandBuffer cmd, const DlssInputs& inputs) -> bool {
	// A missing or mismatched feature was already logged by ensureFeature
	if (m_feature == nullptr || inputs.render_extent != m_feature_render || inputs.output_extent != m_feature_output) {
		return false;
	}
	ZoneScoped;

	auto color = imageResource(inputs.color, inputs.color_image, inputs.color_format, inputs.render_extent, false, false);
	auto depth = imageResource(inputs.depth, inputs.depth_image, inputs.depth_format, inputs.render_extent, true, false);
	auto motion =
	    imageResource(inputs.motion, inputs.motion_image, vk::Format::eR16G16B16A16Sfloat, inputs.render_extent, false, false);
	auto reactive = imageResource(inputs.reactive, inputs.reactive_image, vk::Format::eR8Unorm, inputs.render_extent, false, false);
	auto output = imageResource(inputs.output, inputs.output_image, inputs.color_format, inputs.output_extent, false, true);

	NVSDK_NGX_VK_DLSS_Eval_Params eval {};
	eval.Feature.pInColor = &color;
	eval.Feature.pInOutput = &output;
	eval.pInDepth = &depth;
	eval.pInMotionVectors = &motion;
	eval.pInBiasCurrentColorMask = &reactive;
	eval.InJitterOffsetX = inputs.jitter_pixels.x;
	eval.InJitterOffsetY = inputs.jitter_pixels.y;
	eval.InRenderSubrectDimensions = {inputs.render_extent.width, inputs.render_extent.height};
	eval.InReset = inputs.reset || m_reset_pending ? 1 : 0;
	// Motion is current minus previous in uv and NGX wants previous minus current in render pixels
	eval.InMVScaleX = -static_cast<float>(inputs.render_extent.width);
	eval.InMVScaleY = -static_cast<float>(inputs.render_extent.height);
	eval.InPreExposure = 1.0f;
	eval.InExposureScale = 1.0f;

	const auto result = NGX_VULKAN_EVALUATE_DLSS_EXT(cmd, m_feature, m_parameters, &eval);
	if (NVSDK_NGX_FAILED(result)) {
		setRuntimeReason(std::format("evaluate failed ({})", resultString(result)));
		if (!m_evaluate_failure_logged) {
			m_evaluate_failure_logged = true;
			TOAST_WARN("Render", "DLSS evaluate failed ({}); TAA resolves every frame it fails", resultString(result));
		}
		return false;
	}
	m_reset_pending = false;
	setRuntimeReason({});
	return true;
}

void DlssUpscaler::releaseFeature() noexcept {
	if (m_feature != nullptr) {
		NVSDK_NGX_VULKAN_ReleaseFeature(m_feature);
		m_feature = nullptr;
	}
	m_create_failed = false;
	setRuntimeReason({});
}

void DlssUpscaler::release() noexcept {
	releaseFeature();
	if (m_parameters != nullptr) {
		NVSDK_NGX_VULKAN_DestroyParameters(m_parameters);
		m_parameters = nullptr;
	}
	if (m_ngx_initialized) {
		NVSDK_NGX_VULKAN_Shutdown1(*m_core->getDevice());
		m_ngx_initialized = false;
	}
}

#else

auto DlssUpscaler::instanceExtensions() -> std::vector<const char*> {
	return {};
}

auto DlssUpscaler::deviceExtensions(vk::Instance, vk::PhysicalDevice) -> std::vector<const char*> {
	return {};
}

DlssUpscaler::DlssUpscaler(const VulkanCore& core) : m_core(&core), m_failure_reason("built without DLSS") {
	TOAST_INFO("Render", "DLSS unavailable: {}", m_failure_reason);
}

void DlssUpscaler::ensureFeature(vk::CommandBuffer, vk::Extent2D, vk::Extent2D) { }

auto DlssUpscaler::evaluate(vk::CommandBuffer, const DlssInputs&) -> bool {
	return false;
}

void DlssUpscaler::releaseFeature() noexcept { }

void DlssUpscaler::release() noexcept { }

#endif

DlssUpscaler::~DlssUpscaler() {
	release();
}

}
