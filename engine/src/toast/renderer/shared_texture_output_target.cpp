/// @file SharedTextureOutputTarget.cpp
/// @author dario
/// @date 16/05/2026

#include "shared_texture_output_target.hpp"

#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <cstring>
#include <format>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <vulkan/vulkan_win32.h>
#include <windows.h>
#include <wrl/client.h>
#else
#include <unistd.h>
#endif

namespace renderer {

namespace {

#if defined(_WIN32)
constexpr vk::ExternalMemoryHandleTypeFlagBits k_memory_handle_type = vk::ExternalMemoryHandleTypeFlagBits::eD3D11Texture;
#else
constexpr vk::ExternalMemoryHandleTypeFlagBits k_memory_handle_type = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif

#if defined(_WIN32)
constexpr vk::ImageUsageFlags k_shared_usage = vk::ImageUsageFlagBits::eTransferDst;
#else
constexpr vk::ImageUsageFlags k_shared_usage =
    vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
#endif

#if defined(_WIN32)
constexpr vk::ImageLayout k_shared_release_layout = vk::ImageLayout::eGeneral;
#else
constexpr vk::ImageLayout k_shared_release_layout = vk::ImageLayout::eColorAttachmentOptimal;
#endif

[[nodiscard]]
auto findMemoryType(const vk::raii::PhysicalDevice& physical, uint32_t type_bits) -> std::optional<uint32_t> {
	const auto properties = physical.getMemoryProperties();
	for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
		if ((type_bits & (1u << i)) != 0 && (properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) ==
		                                        vk::MemoryPropertyFlagBits::eDeviceLocal) {
			return i;
		}
	}
	for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
		if ((type_bits & (1u << i)) != 0) {
			return i;
		}
	}
	return std::nullopt;
}

[[nodiscard]]
auto canShareImages(const vk::raii::PhysicalDevice& physical, vk::Format format) -> bool {
	vk::PhysicalDeviceExternalImageFormatInfo external_info(k_memory_handle_type);
	vk::PhysicalDeviceImageFormatInfo2 info(
	    format, vk::ImageType::e2D, vk::ImageTiling::eOptimal, k_shared_usage, {}, &external_info
	);
	try {
		const auto chain = physical.getImageFormatProperties2<vk::ImageFormatProperties2, vk::ExternalImageFormatProperties>(info);
		const auto features = chain.get<vk::ExternalImageFormatProperties>().externalMemoryProperties.externalMemoryFeatures;
#if defined(_WIN32)
		return static_cast<bool>(features & vk::ExternalMemoryFeatureFlagBits::eImportable);
#else
		return static_cast<bool>(features & vk::ExternalMemoryFeatureFlagBits::eExportable);
#endif
	} catch (const vk::SystemError&) { return false; }
}

}

struct SharedTextureOutputTarget::Interop {
	struct Slot {
		vk::raii::Image image = nullptr;
		vk::raii::DeviceMemory memory = nullptr;
		bool held = false;
#if defined(_WIN32)
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		HANDLE handle = nullptr;
		VkDeviceMemory sync_memory = VK_NULL_HANDLE;
		uint64_t key = 0;
		uint32_t timeout = INFINITE;
		VkWin32KeyedMutexAcquireReleaseInfoKHR keyed_mutex {};
#else
		vk::raii::Semaphore ready = nullptr;
		vk::raii::Semaphore done = nullptr;
		bool ready_signaled = false;
		bool done_pending = false;
#endif
	};

	std::vector<Slot> slots;
	vk::DeviceSize memory_size = 0;
	uint64_t generation = 0;
	int32_t displayed = -1;
	uint64_t adapter_luid = 0;
	std::array<uint8_t, VK_UUID_SIZE> adapter_uuid {};
#if defined(_WIN32)
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	PFN_vkGetMemoryWin32HandlePropertiesKHR get_handle_properties = nullptr;
#endif

	~Interop() {
#if defined(_WIN32)
		for (Slot& slot : slots) {
			if (slot.handle != nullptr) {
				CloseHandle(slot.handle);
			}
		}
#endif
	}

	void createSlots(const VulkanCore& core, vk::Extent2D extent, vk::Format format, size_t count) {
		const auto& device = core.getDevice();
		slots.clear();
		displayed = -1;
		slots.resize(count);
		++generation;

		for (size_t index = 0; index < count; ++index) {
			Slot& slot = slots[index];

			vk::ExternalMemoryImageCreateInfo external_ci(k_memory_handle_type);
			vk::ImageCreateInfo image_ci {};
			image_ci.pNext = &external_ci;
			image_ci.imageType = vk::ImageType::e2D;
			image_ci.format = format;
			image_ci.extent = vk::Extent3D {extent.width, extent.height, 1};
			image_ci.mipLevels = 1;
			image_ci.arrayLayers = 1;
			image_ci.samples = vk::SampleCountFlagBits::e1;
			image_ci.tiling = vk::ImageTiling::eOptimal;
			image_ci.usage = k_shared_usage;
			image_ci.sharingMode = vk::SharingMode::eExclusive;
			image_ci.initialLayout = vk::ImageLayout::eUndefined;
			slot.image = vk::raii::Image(device, image_ci);
			setDebugName(core, *slot.image, std::format("SharedTextureOutput Shared[{}]", index));

			const vk::MemoryRequirements requirements = slot.image.getMemoryRequirements();
			vk::MemoryDedicatedAllocateInfo dedicated(*slot.image);
			vk::MemoryAllocateInfo allocate_info(requirements.size, 0);

#if defined(_WIN32)
			D3D11_TEXTURE2D_DESC desc {};
			desc.Width = extent.width;
			desc.Height = extent.height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
			if (FAILED(this->device->CreateTexture2D(&desc, nullptr, &slot.texture))) {
				throw std::runtime_error("CreateTexture2D failed");
			}
			Microsoft::WRL::ComPtr<IDXGIResource1> resource;
			if (FAILED(slot.texture.As(&resource)) ||
			    FAILED(
			        resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &slot.handle)
			    )) {
				throw std::runtime_error("CreateSharedHandle failed");
			}

			VkMemoryWin32HandlePropertiesKHR handle_properties {VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
			const VkResult properties_result = get_handle_properties(
			    static_cast<VkDevice>(*device),
			    static_cast<VkExternalMemoryHandleTypeFlagBits>(k_memory_handle_type),
			    slot.handle,
			    &handle_properties
			);
			const uint32_t type_bits =
			    properties_result == VK_SUCCESS && (handle_properties.memoryTypeBits & requirements.memoryTypeBits) != 0
			        ? handle_properties.memoryTypeBits & requirements.memoryTypeBits
			        : requirements.memoryTypeBits;

			VkImportMemoryWin32HandleInfoKHR import_info {VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
			import_info.handleType = static_cast<VkExternalMemoryHandleTypeFlagBits>(k_memory_handle_type);
			import_info.handle = slot.handle;
			import_info.pNext = &dedicated;
			allocate_info.pNext = &import_info;
#else
			const uint32_t type_bits = requirements.memoryTypeBits;
			vk::ExportMemoryAllocateInfo export_info(k_memory_handle_type, &dedicated);
			allocate_info.pNext = &export_info;
#endif
			const auto type_index = findMemoryType(core.getPhysicalDevice(), type_bits);
			if (!type_index) {
				throw std::runtime_error("no memory type fits the shared image");
			}
			allocate_info.memoryTypeIndex = *type_index;
			slot.memory = vk::raii::DeviceMemory(device, allocate_info);
			slot.image.bindMemory(*slot.memory, 0);
			memory_size = requirements.size;

#if defined(_WIN32)
			slot.sync_memory = static_cast<VkDeviceMemory>(*slot.memory);
			slot.keyed_mutex = VkWin32KeyedMutexAcquireReleaseInfoKHR {VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR};
			slot.keyed_mutex.acquireCount = 1;
			slot.keyed_mutex.pAcquireSyncs = &slot.sync_memory;
			slot.keyed_mutex.pAcquireKeys = &slot.key;
			slot.keyed_mutex.pAcquireTimeouts = &slot.timeout;
			slot.keyed_mutex.releaseCount = 1;
			slot.keyed_mutex.pReleaseSyncs = &slot.sync_memory;
			slot.keyed_mutex.pReleaseKeys = &slot.key;
#else
			vk::ExportSemaphoreCreateInfo export_semaphore(vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd);
			const vk::SemaphoreCreateInfo semaphore_ci({}, &export_semaphore);
			slot.ready = vk::raii::Semaphore(device, semaphore_ci);
			slot.done = vk::raii::Semaphore(device, semaphore_ci);
#endif
		}
	}
};

SharedTextureOutputTarget::SharedTextureOutputTarget(const VulkanCore& core, vk::Extent2D preferred_extent, uint32_t image_count)
    : m_core(&core),
      m_extent(preferred_extent),
      m_images(image_count) {
	if (image_count == 0) {
		TOAST_CRITICAL("Render", "Toast Engine Error: Shared texture output target needs at least one image!");
	}
	allocateResources(m_extent);
}

SharedTextureOutputTarget::~SharedTextureOutputTarget() = default;

void SharedTextureOutputTarget::allocateResources(vk::Extent2D extent) {
	ZoneScoped;
	m_extent = extent;
	if (m_extent.width == 0 || m_extent.height == 0) {
		TOAST_CRITICAL("Render", "Toast Engine Error: Shared texture output target needs a non-zero extent!");
	}

	const vk::DeviceSize staging_size = imageByteSize();

	for (uint32_t index = 0; index < m_images.size(); ++index) {
		auto& shared = m_images[index];
		shared.mapped = nullptr;
		shared.read_back = false;
		shared.shared = false;
		shared.view.reset();
		shared.staging.reset();
		shared.image.reset();

		vk::ImageCreateInfo image_ci {};
		image_ci.imageType = vk::ImageType::e2D;
		image_ci.format = m_color_format;
		image_ci.extent = vk::Extent3D {m_extent.width, m_extent.height, 1};
		image_ci.mipLevels = 1;
		image_ci.arrayLayers = 1;
		image_ci.samples = vk::SampleCountFlagBits::e1;
		image_ci.tiling = vk::ImageTiling::eOptimal;
		image_ci.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
		image_ci.sharingMode = vk::SharingMode::eExclusive;
		image_ci.initialLayout = vk::ImageLayout::eUndefined;

		vma::AllocationCreateInfo image_alloc_ci {};
		image_alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

		shared.image.emplace(m_core->getAllocator().createImage(image_ci, image_alloc_ci));
		setDebugName(*m_core, **shared.image, std::format("SharedTextureOutput Image[{}]", index));

		vk::ImageViewCreateInfo view_ci {};
		view_ci.image = **shared.image;
		view_ci.viewType = vk::ImageViewType::e2D;
		view_ci.format = m_color_format;
		view_ci.subresourceRange = colorSubresourceRange();
		shared.view.emplace(m_core->getDevice(), view_ci);
		setDebugName(*m_core, **shared.view, std::format("SharedTextureOutput ImageView[{}]", index));

		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = staging_size;
		buffer_ci.usage = vk::BufferUsageFlagBits::eTransferDst;
		buffer_ci.sharingMode = vk::SharingMode::eExclusive;

		vma::AllocationCreateInfo buffer_alloc_ci {};
		buffer_alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		buffer_alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessRandom;

		shared.staging.emplace(m_core->getAllocator().createBuffer(buffer_ci, buffer_alloc_ci));
		setDebugName(*m_core, **shared.staging, std::format("SharedTextureOutput StagingBuffer[{}]", index));
		shared.mapped = shared.staging->getAllocation().getInfo().pMappedData;
	}

	if (m_interop && m_sharing.load(std::memory_order_acquire) != SharingState::active) {
		m_interop.reset();
	}
	if (m_interop) {
		try {
			m_interop->createSlots(*m_core, m_extent, m_color_format, m_images.size());
		} catch (const std::exception& error) {
			TOAST_WARN("Render", "Could not recreate the shared viewport textures ({}); reading frames back instead", error.what());
			m_interop.reset();
			m_sharing.store(SharingState::failed, std::memory_order_release);
		}
	}

	TOAST_TRACE("Render", "Allocated {} shared images at {}x{}", m_images.size(), m_extent.width, m_extent.height);
}

void SharedTextureOutputTarget::createInterop() {
	ZoneScoped;
	auto interop = std::make_unique<Interop>();
	try {
#if defined(_WIN32)
		interop->get_handle_properties = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
		    m_core->getDevice().getProcAddr("vkGetMemoryWin32HandlePropertiesKHR")
		);
		if (interop->get_handle_properties == nullptr) {
			throw std::runtime_error("vkGetMemoryWin32HandlePropertiesKHR is missing");
		}

		// The textures live on the adapter Vulkan renders with, which enableSharedTextures() checked is the compositor's
		const auto ids = m_core->getPhysicalDevice().getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
		LUID luid {};
		std::memcpy(&luid, ids.get<vk::PhysicalDeviceIDProperties>().deviceLUID.data(), sizeof(luid));

		Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)))) {
			throw std::runtime_error("the Vulkan adapter has no DXGI adapter");
		}
		const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
		if (FAILED(D3D11CreateDevice(
		        adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &interop->device, nullptr, nullptr
		    ))) {
			throw std::runtime_error("D3D11CreateDevice failed");
		}
#endif
		interop->createSlots(*m_core, m_extent, m_color_format, m_images.size());
	} catch (const std::exception& error) {
		TOAST_WARN("Render", "Could not share the viewport textures ({}); reading frames back instead", error.what());
		m_sharing.store(SharingState::failed, std::memory_order_release);
		return;
	}

	TOAST_INFO("Render", "Viewport frames are shared with the compositor as textures");
	{
		std::scoped_lock lock(m_frame_mutex);
		m_interop = std::move(interop);
	}
	m_sharing.store(SharingState::active, std::memory_order_release);
}

auto SharedTextureOutputTarget::getColorImage(uint32_t index) const -> const vk::Image& {
	return **m_images.at(index).image;
}

auto SharedTextureOutputTarget::getColorAttachment(uint32_t index) const -> const vk::raii::ImageView& {
	return *m_images.at(index).view;
}

auto SharedTextureOutputTarget::acquireNextImage(uint64_t, vk::Semaphore, vk::Fence) -> vk::ResultValue<uint32_t> {
	ZoneScoped;
	if (m_sharing.load(std::memory_order_acquire) == SharingState::requested) {
		createInterop();
	}
	const std::scoped_lock lock(m_frame_mutex);

	const uint32_t count = getImageCount();
	const int32_t published = m_latest_ready.load(std::memory_order_acquire);
	const auto busy = [&](uint32_t index) {
		return std::cmp_equal(index, published) || std::cmp_equal(index, m_reading) ||
		       (m_interop && index < m_interop->slots.size() &&
		        (m_interop->slots[index].held || std::cmp_equal(index, m_interop->displayed)));
	};

	uint32_t image_index = m_next_acquire_index;
	for (uint32_t step = 0; step < count; ++step) {
		const uint32_t candidate = (m_next_acquire_index + step) % count;
		if (!busy(candidate)) {
			image_index = candidate;
			break;
		}
	}
	m_next_acquire_index = (image_index + 1) % count;

	return {vk::Result::eSuccess, image_index};
}

auto SharedTextureOutputTarget::present(uint32_t, vk::Semaphore) -> vk::Result {
	return vk::Result::eSuccess;
}

void SharedTextureOutputTarget::recordFinalize(vk::CommandBuffer command_buffer, uint32_t image_index) {
	ZoneScoped;
	auto& shared = m_images.at(image_index);
	const vk::Image image = **shared.image;
	const vk::Buffer staging = **shared.staging;

	shared.shared = m_interop != nullptr && m_sharing.load(std::memory_order_acquire) == SharingState::active;
	shared.read_back = !shared.shared || m_readback_requested.exchange(false, std::memory_order_acq_rel);

	const vk::ImageMemoryBarrier to_transfer(
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    vk::AccessFlagBits::eTransferRead,
	    vk::ImageLayout::eColorAttachmentOptimal,
	    vk::ImageLayout::eTransferSrcOptimal,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    image,
	    colorSubresourceRange()
	);
	command_buffer.pipelineBarrier(
	    vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, to_transfer
	);

	if (shared.shared) {
		const vk::Image target = *m_interop->slots.at(image_index).image;
		const uint32_t graphics_family = m_core->getGraphicsQueueFamilyIndex();

		const vk::ImageMemoryBarrier acquire(
		    {},
		    vk::AccessFlagBits::eTransferWrite,
		    vk::ImageLayout::eUndefined,
		    vk::ImageLayout::eTransferDstOptimal,
		    VK_QUEUE_FAMILY_EXTERNAL,
		    graphics_family,
		    target,
		    colorSubresourceRange()
		);
		command_buffer.pipelineBarrier(
		    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, acquire
		);

		const vk::ImageCopy copy(
		    vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
		    vk::Offset3D {0, 0, 0},
		    vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
		    vk::Offset3D {0, 0, 0},
		    vk::Extent3D {m_extent.width, m_extent.height, 1}
		);
		command_buffer.copyImage(image, vk::ImageLayout::eTransferSrcOptimal, target, vk::ImageLayout::eTransferDstOptimal, copy);

		const vk::ImageMemoryBarrier release(
		    vk::AccessFlagBits::eTransferWrite,
		    {},
		    vk::ImageLayout::eTransferDstOptimal,
		    k_shared_release_layout,
		    graphics_family,
		    VK_QUEUE_FAMILY_EXTERNAL,
		    target,
		    colorSubresourceRange()
		);
		command_buffer.pipelineBarrier(
		    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, release
		);
	}

	if (!shared.read_back) {
		return;
	}

	const vk::BufferImageCopy region(
	    0,
	    0,
	    0,
	    vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
	    vk::Offset3D {0, 0, 0},
	    vk::Extent3D {m_extent.width, m_extent.height, 1}
	);
	command_buffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, staging, region);

	const vk::BufferMemoryBarrier to_host(
	    vk::AccessFlagBits::eTransferWrite,
	    vk::AccessFlagBits::eHostRead,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    staging,
	    0,
	    VK_WHOLE_SIZE
	);
	command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, {}, to_host, {});
}

auto SharedTextureOutputTarget::submitSync(uint32_t image_index) -> OutputSubmitSync {
	if (image_index >= m_images.size() || !m_images[image_index].shared || !m_interop || image_index >= m_interop->slots.size()) {
		return {};
	}
	auto& slot = m_interop->slots[image_index];
	OutputSubmitSync sync;
#if defined(_WIN32)
	sync.chain = &slot.keyed_mutex;
#else
	const std::scoped_lock lock(m_frame_mutex);
	if (slot.done_pending) {
		sync.wait = *slot.done;
		sync.wait_stage = vk::PipelineStageFlagBits::eTransfer;
		slot.done_pending = false;
	}
	if (!slot.ready_signaled) {
		sync.signal = *slot.ready;
		slot.ready_signaled = true;
	}
#endif
	return sync;
}

void SharedTextureOutputTarget::onImageRenderComplete(uint32_t image_index) {
	ZoneScoped;
	if (image_index >= m_images.size()) {
		return;
	}
	// Same lock as copyLatestFrame() so the index and counter agree
	const std::scoped_lock lock(m_frame_mutex);

	auto& shared = m_images[image_index];
	if (shared.read_back && shared.staging.has_value()) {
		shared.staging->getAllocation().invalidate(0, imageByteSize());
		m_latest_readback.store(static_cast<int32_t>(image_index), std::memory_order_release);
	}
	m_latest_ready.store(static_cast<int32_t>(image_index), std::memory_order_release);
	m_frame_counter.fetch_add(1, std::memory_order_acq_rel);
}

auto SharedTextureOutputTarget::copyLatestFrame(void* dst, uint32_t dst_capacity, ViewportFrameDesc* out) -> int {
	ZoneScoped;
	const uint32_t row_pitch = m_extent.width * 4;
	const uint32_t required = row_pitch * m_extent.height;
	const void* src = nullptr;
	int32_t index = -1;
	{
		std::scoped_lock lock(m_frame_mutex);
		index = m_latest_readback.load(std::memory_order_acquire);
		if (index < 0 || static_cast<size_t>(index) >= m_images.size()) {
			return 0;
		}
		if (out) {
			out->width = m_extent.width;
			out->height = m_extent.height;
			out->row_pitch = row_pitch;
			out->frame_id = m_frame_counter.load(std::memory_order_acquire);
		}
		src = m_images[static_cast<size_t>(index)].mapped;
		if (dst == nullptr || src == nullptr || dst_capacity < required) {
			return -1;
		}
		// The renderer skips this slot until the copy is done, so it runs without holding the render thread up
		m_reading = index;
	}

	std::memcpy(dst, src, required);

	{
		std::scoped_lock lock(m_frame_mutex);
		m_reading = -1;
	}
	m_reading_done.notify_all();
	return 1;
}

void SharedTextureOutputTarget::requestReadback() {
	std::scoped_lock lock(m_frame_mutex);
	m_latest_readback.store(-1, std::memory_order_release);
	m_readback_requested.store(true, std::memory_order_release);
}

void SharedTextureOutputTarget::disableSharedTextures() {
	if (m_sharing.exchange(SharingState::failed, std::memory_order_acq_rel) != SharingState::off) {
		TOAST_INFO("Render", "The host stopped using shared viewport textures; reading frames back instead");
	}
}

auto SharedTextureOutputTarget::enableSharedTextures(uint64_t adapter_luid, const uint8_t* adapter_uuid) -> bool {
	if (m_core->getNsightMode() != NsightMode::none || m_core->getRenderDocAPI() != nullptr) {
		TOAST_INFO("Render", "A graphics debugger is attached; the viewport reads frames back instead of sharing textures");
		return false;
	}
	if (!m_core->isTextureSharingSupported() || !canShareImages(m_core->getPhysicalDevice(), m_color_format)) {
		return false;
	}
	const auto ids = m_core->getPhysicalDevice().getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
	const auto& id = ids.get<vk::PhysicalDeviceIDProperties>();
#if defined(_WIN32)
	uint64_t luid = 0;
	std::memcpy(&luid, id.deviceLUID.data(), sizeof(luid));
	if (!id.deviceLUIDValid || adapter_luid == 0 || luid != adapter_luid) {
		TOAST_INFO("Render", "The compositor runs on another adapter than Vulkan; the viewport reads frames back instead");
		return false;
	}
#else
	(void)adapter_luid;
	if (adapter_uuid == nullptr || std::memcmp(id.deviceUUID.data(), adapter_uuid, VK_UUID_SIZE) != 0) {
		TOAST_INFO("Render", "The compositor runs on another device than Vulkan; the viewport reads frames back instead");
		return false;
	}
	const auto semaphore_properties =
	    m_core->getPhysicalDevice().getExternalSemaphoreProperties({vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd});
	if (!(semaphore_properties.externalSemaphoreFeatures & vk::ExternalSemaphoreFeatureFlagBits::eExportable)) {
		return false;
	}
#endif
	(void)adapter_uuid;

	SharingState expected = SharingState::off;
	m_sharing.compare_exchange_strong(expected, SharingState::requested, std::memory_order_acq_rel);
	return m_sharing.load(std::memory_order_acquire) != SharingState::failed;
}

auto SharedTextureOutputTarget::sharedFrame(ViewportSharedFrameDesc* out) -> int {
	const SharingState state = m_sharing.load(std::memory_order_acquire);
	if (state == SharingState::off || state == SharingState::failed) {
		return -1;
	}
	std::scoped_lock lock(m_frame_mutex);
	const int32_t index = m_latest_ready.load(std::memory_order_acquire);
	if (state != SharingState::active || !m_interop || index < 0 || !m_images[static_cast<size_t>(index)].shared) {
		return 0;
	}
	if (out) {
		out->width = m_extent.width;
		out->height = m_extent.height;
		out->frame_id = m_frame_counter.load(std::memory_order_acquire);
		out->generation = m_interop->generation;
		out->memory_size = m_interop->memory_size;
		out->slot = static_cast<uint32_t>(index);
		out->slot_count = static_cast<uint32_t>(m_interop->slots.size());
	}
	return 1;
}

auto SharedTextureOutputTarget::exportSharedSlot(uint64_t generation, uint32_t slot_index, ViewportSharedHandles* out) -> bool {
	std::scoped_lock lock(m_frame_mutex);
	if (out == nullptr || !m_interop || m_interop->generation != generation || slot_index >= m_interop->slots.size()) {
		return false;
	}
	auto& slot = m_interop->slots[slot_index];
#if defined(_WIN32)
	out->image = reinterpret_cast<intptr_t>(slot.handle);
	out->ready = 0;
	out->done = 0;
#else
	const auto& device = m_core->getDevice();
	try {
		out->image = device.getMemoryFdKHR({*slot.memory, k_memory_handle_type});
		out->ready = device.getSemaphoreFdKHR({*slot.ready, vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd});
		out->done = device.getSemaphoreFdKHR({*slot.done, vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd});
	} catch (const vk::SystemError& error) {
		TOAST_WARN("Render", "Could not export shared viewport slot {}: {}", slot_index, error.what());
		for (const intptr_t fd : {out->image, out->ready, out->done}) {
			if (fd > 0) {
				close(static_cast<int>(fd));
			}
		}
		*out = {};
		return false;
	}
#endif
	return true;
}

auto SharedTextureOutputTarget::holdSharedSlot(uint64_t generation, uint32_t slot) -> bool {
	std::scoped_lock lock(m_frame_mutex);
	if (!m_interop || m_interop->generation != generation || slot >= m_interop->slots.size()) {
		return false;
	}
	m_interop->slots[slot].held = true;
	return true;
}

void SharedTextureOutputTarget::releaseSharedSlot(uint64_t generation, uint32_t slot, bool consumed) {
	std::scoped_lock lock(m_frame_mutex);
	if (!m_interop || m_interop->generation != generation || slot >= m_interop->slots.size()) {
		return;
	}
	auto& shared = m_interop->slots[slot];
	shared.held = false;
	if (consumed) {
		m_interop->displayed = static_cast<int32_t>(slot);
	}
#if !defined(_WIN32)
	if (consumed) {
		shared.ready_signaled = false;
		shared.done_pending = true;
	}
#else
	(void)consumed;
#endif
}

void SharedTextureOutputTarget::recreate(vk::Extent2D extent) {
	std::unique_lock lock(m_frame_mutex);
	m_reading_done.wait(lock, [this] { return m_reading < 0; });
	m_latest_ready.store(-1, std::memory_order_release);
	m_latest_readback.store(-1, std::memory_order_release);
	m_next_acquire_index = 0;
	allocateResources(extent);
}

}
