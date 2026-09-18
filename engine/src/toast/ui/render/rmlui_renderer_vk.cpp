// NOLINTBEGIN

#include "rmlui_renderer_vk.h"

#include "../tga_decode.hpp"
#include "shaders_compiled_spv.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/Platform.h>
#include <RmlUi/Core/Profiling.h>
#include <algorithm>
#include <string.h>
#include <tracy/Tracy.hpp>

// AlignUp(314, 256) = 512
template<typename T>
static T AlignUp(T val, T alignment) {
	return (val + alignment - (T)1) & ~(alignment - (T)1);
}

VkValidationFeaturesEXT debug_validation_features_ext = {};
VkValidationFeatureEnableEXT debug_validation_features_ext_requested[] = {
  VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
  VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
  VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
};

#ifdef RMLUI_VK_DEBUG
static Rml::String FormatByteSize(VkDeviceSize size) noexcept {
	constexpr VkDeviceSize K = VkDeviceSize(1024);
	if (size < K) {
		return Rml::CreateString("%zu B", size);
	} else if (size < K * K) {
		return Rml::CreateString("%g KB", double(size) / double(K));
	}
	return Rml::CreateString("%g MB", double(size) / double(K * K));
}

static VKAPI_ATTR VkBool32 VKAPI_CALL MyDebugReportCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severityFlags, VkDebugUtilsMessageTypeFlagsEXT /*messageTypeFlags*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* /*pUserData*/
) {
	if (severityFlags & VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
		return VK_FALSE;
	}

#ifdef RMLUI_PLATFORM_WIN32
	if (severityFlags & VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		// some logs are not passed to our UI, because of early calling for explicity I put native log output
		OutputDebugString(TEXT("\n"));
		OutputDebugStringA(pCallbackData->pMessage);
	}
#endif

	Rml::Log::Message(Rml::Log::LT_ERROR, "[Vulkan][VALIDATION] %s ", pCallbackData->pMessage);

	return VK_FALSE;
}
#endif

RenderInterface_VK::RenderInterface_VK()
    : m_is_apply_to_regular_geometry_stencil {false},
      m_is_use_scissor_specified {false},
      m_is_use_stencil_pipeline {false},
      m_width {},
      m_height {},
      m_queue_index_graphics {},
      m_frame_index {},
      m_p_instance {},
      m_p_device {},
      m_p_physical_device {},
      m_p_allocator {},
      m_p_current_command_buffer {},
      m_color_attachment_format {VK_FORMAT_UNDEFINED},
      m_depth_stencil_attachment_format {VK_FORMAT_UNDEFINED},
      m_p_submit_mutex {},
      m_p_descriptor_set_layout_vertex_transform {},
      m_p_descriptor_set_layout_texture {},
      m_p_pipeline_layout {},
      m_p_pipeline_with_textures {},
      m_p_pipeline_without_textures {},
      m_p_pipeline_stencil_for_region_where_geometry_will_be_drawn {},
      m_p_pipeline_stencil_for_regular_geometry_that_applied_to_region_with_textures {},
      m_p_pipeline_stencil_for_regular_geometry_that_applied_to_region_without_textures {},
      m_p_descriptor_set {},
      m_p_sampler_linear {},
      m_scissor {},
      m_scissor_original {},
      m_viewport {} { }

RenderInterface_VK::~RenderInterface_VK() { }

Rml::CompiledGeometryHandle
    RenderInterface_VK::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
	RMLUI_ZoneScopedN("Vulkan - CompileGeometry");

	if (vertices.empty() || indices.empty()) {
		return {};
	}

	auto geometry = std::make_unique<geometry_handle_t>();
	uint32_t* pCopyDataToBuffer = nullptr;
	const void* pData = reinterpret_cast<const void*>(vertices.data());

	bool status = m_memory_pool.Alloc_VertexBuffer(
	    (uint32_t)vertices.size(),
	    sizeof(Rml::Vertex),
	    reinterpret_cast<void**>(&pCopyDataToBuffer),
	    &geometry->m_p_vertex,
	    &geometry->m_p_vertex_allocation
	);
	if (!status) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "RmlUi vertex buffer pool exhausted while compiling geometry.");
		return {};
	}

	memcpy(pCopyDataToBuffer, pData, sizeof(Rml::Vertex) * vertices.size());
	m_memory_pool.Flush(geometry->m_p_vertex);

	status = m_memory_pool.Alloc_IndexBuffer(
	    (uint32_t)indices.size(),
	    sizeof(int),
	    reinterpret_cast<void**>(&pCopyDataToBuffer),
	    &geometry->m_p_index,
	    &geometry->m_p_index_allocation
	);
	if (!status) {
		m_memory_pool.Free_Allocation(geometry->m_p_vertex_allocation);
		Rml::Log::Message(Rml::Log::LT_ERROR, "RmlUi index buffer pool exhausted while compiling geometry.");
		return {};
	}

	memcpy(pCopyDataToBuffer, indices.data(), sizeof(int) * indices.size());
	m_memory_pool.Flush(geometry->m_p_index);

	geometry->m_num_indices = (int)indices.size();

	auto owner = std::shared_ptr<geometry_handle_t>(geometry.release(), [this](geometry_handle_t* resource) {
		m_memory_pool.Free_GeometryHandle(resource);
		delete resource;
	});
	geometry_handle_t* handle = owner.get();
	m_live_resources.emplace(handle, std::move(owner));
	return Rml::CompiledGeometryHandle(handle);
}

void RenderInterface_VK::RenderGeometry(
    Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture
) {
	RMLUI_ZoneScopedN("Vulkan - RenderCompiledGeometry");

	if (m_p_current_command_buffer == nullptr) {
		return;
	}

	texture_data_t* p_texture = reinterpret_cast<texture_data_t*>(texture);
	geometry_handle_t* p_casted_compiled_geometry = reinterpret_cast<geometry_handle_t*>(geometry);
	if (!p_casted_compiled_geometry) {
		return;
	}

	if (!RetainResource(p_casted_compiled_geometry) || (p_texture && !RetainResource(p_texture))) {
		return;
	}

	VkDescriptorImageInfo info_descriptor_image = {};
	if (p_texture && p_texture->m_p_vk_descriptor_set == nullptr) {
		VkDescriptorSet p_texture_set = nullptr;
		if (!m_manager_descriptors.Alloc_Descriptor(m_p_device, &m_p_descriptor_set_layout_texture, &p_texture_set)) {
			Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to allocate an RmlUi texture descriptor set.");
			return;
		}

		info_descriptor_image.imageView = p_texture->m_p_vk_image_view;
		info_descriptor_image.sampler = p_texture->m_p_vk_sampler;
		info_descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet info_write = {};

		info_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		info_write.dstSet = p_texture_set;
		info_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		info_write.dstBinding = 2;
		info_write.pImageInfo = &info_descriptor_image;
		info_write.descriptorCount = 1;

		vkUpdateDescriptorSets(m_p_device, 1, &info_write, 0, nullptr);
		p_texture->m_p_vk_descriptor_set = p_texture_set;
	}

	m_user_data_for_vertex_shader.m_translate = translation;

	VkDescriptorSet p_current_descriptor_set = nullptr;
	p_current_descriptor_set = m_p_descriptor_set;

	RMLUI_VK_ASSERTMSG(
	    p_current_descriptor_set,
	    "you can't have here an invalid pointer of VkDescriptorSet. Two reason might be. 1. - you didn't allocate it "
	    "at all or 2. - Somehing is wrong with allocation and somehow it was corrupted by something."
	);

	VkDescriptorBufferInfo shader_buffer {};
	VmaVirtualAllocation shader_allocation = nullptr;
	shader_vertex_user_data_t* p_data = nullptr;
	const bool status = m_memory_pool.Alloc_GeneralBuffer(
	    sizeof(m_user_data_for_vertex_shader), reinterpret_cast<void**>(&p_data), &shader_buffer, &shader_allocation
	);
	if (!status) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "RmlUi uniform buffer pool exhausted while rendering geometry.");
		return;
	}
	m_command_buffer_ring.AddCurrentSlotAllocation(shader_allocation);

	p_data->m_transform = m_user_data_for_vertex_shader.m_transform;
	p_data->m_translate = m_user_data_for_vertex_shader.m_translate;
	m_memory_pool.Flush(shader_buffer);

	const uint32_t pDescriptorOffsets = static_cast<uint32_t>(shader_buffer.offset);

	VkDescriptorSet p_texture_descriptor_set = nullptr;

	if (p_texture) {
		p_texture_descriptor_set = p_texture->m_p_vk_descriptor_set;
	}

	VkDescriptorSet p_sets[] = {p_current_descriptor_set, p_texture_descriptor_set};
	int real_size_of_sets = 2;

	if (p_texture == nullptr) {
		real_size_of_sets = 1;
	}

	vkCmdBindDescriptorSets(
	    m_p_current_command_buffer,
	    VK_PIPELINE_BIND_POINT_GRAPHICS,
	    m_p_pipeline_layout,
	    0,
	    real_size_of_sets,
	    p_sets,
	    1,
	    &pDescriptorOffsets
	);

	if (m_is_use_stencil_pipeline) {
		// clip mask writes pick the op and reference set up by RenderToClipMask
		vkCmdBindPipeline(
		    m_p_current_command_buffer,
		    VK_PIPELINE_BIND_POINT_GRAPHICS,
		    m_clip_incr ? m_p_pipeline_clip_write_incr : m_p_pipeline_stencil_for_region_where_geometry_will_be_drawn
		);
		vkCmdSetStencilReference(m_p_current_command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, m_clip_write_value);
	} else {
		if (p_texture) {
			if (m_is_apply_to_regular_geometry_stencil) {
				vkCmdBindPipeline(
				    m_p_current_command_buffer,
				    VK_PIPELINE_BIND_POINT_GRAPHICS,
				    m_p_pipeline_stencil_for_regular_geometry_that_applied_to_region_with_textures
				);
			} else {
				vkCmdBindPipeline(m_p_current_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_p_pipeline_with_textures);
			}
		} else {
			if (m_is_apply_to_regular_geometry_stencil) {
				vkCmdBindPipeline(
				    m_p_current_command_buffer,
				    VK_PIPELINE_BIND_POINT_GRAPHICS,
				    m_p_pipeline_stencil_for_regular_geometry_that_applied_to_region_without_textures
				);
			} else {
				vkCmdBindPipeline(m_p_current_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_p_pipeline_without_textures);
			}
		}
	}

	vkCmdBindVertexBuffers(
	    m_p_current_command_buffer,
	    0,
	    1,
	    &p_casted_compiled_geometry->m_p_vertex.buffer,
	    &p_casted_compiled_geometry->m_p_vertex.offset
	);

	vkCmdBindIndexBuffer(
	    m_p_current_command_buffer,
	    p_casted_compiled_geometry->m_p_index.buffer,
	    p_casted_compiled_geometry->m_p_index.offset,
	    VK_INDEX_TYPE_UINT32
	);

	vkCmdDrawIndexed(m_p_current_command_buffer, p_casted_compiled_geometry->m_num_indices, 1, 0, 0, 0);
}

void RenderInterface_VK::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
	RMLUI_ZoneScopedN("Vulkan - ReleaseCompiledGeometry");
	ReleaseResource(reinterpret_cast<const void*>(geometry));
}

void RenderInterface_VK::EnableScissorRegion(bool enable) {
	if (m_p_current_command_buffer == nullptr) {
		return;
	}

	m_is_use_scissor_specified = enable;

	if (!m_is_use_scissor_specified) {
		vkCmdSetScissor(m_p_current_command_buffer, 0, 1, &m_scissor_original);
	}
}

void RenderInterface_VK::SetScissorRegion(Rml::Rectanglei region) {
	if (!m_is_use_scissor_specified || m_p_current_command_buffer == nullptr) {
		return;
	}

	const int left = Rml::Math::Clamp(region.Left(), 0, m_width);
	const int top = Rml::Math::Clamp(region.Top(), 0, m_height);
	const int right = Rml::Math::Clamp(region.Right(), 0, m_width);
	const int bottom = Rml::Math::Clamp(region.Bottom(), 0, m_height);
	const int width = std::max(right - left, 0);
	const int height = std::max(bottom - top, 0);

	m_scissor.offset = {left, top};
	m_scissor.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
	vkCmdSetScissor(m_p_current_command_buffer, 0, 1, &m_scissor);
}

Rml::TextureHandle RenderInterface_VK::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
	Rml::FileInterface* file_interface = Rml::GetFileInterface();
	Rml::FileHandle file_handle = file_interface->Open(source);
	if (!file_handle) {
		return false;
	}

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	using Rml::byte;
	Rml::UniquePtr<byte[]> buffer(new byte[buffer_size]);
	file_interface->Read(buffer.get(), buffer_size, file_handle);
	file_interface->Close(file_handle);

	// toast-engine adaptation: decode through the engine TGA decoder (adds RLE support), then
	// convert to premultiplied alpha as RmlUi expects
	auto decoded = ui::decodeTga({buffer.get(), buffer_size});
	if (!decoded) {
		Rml::Log::Message(
		    Rml::Log::LT_ERROR, "Failed to decode TGA '%s': only 24/32bit true-color images are supported.", source.c_str()
		);
		return false;
	}

	byte* pixels = decoded->pixels.data();
	const size_t pixel_count = size_t(decoded->width) * decoded->height;
	for (size_t i = 0; i < pixel_count; i++) {
		const byte alpha = pixels[i * 4 + 3];
		if (alpha < 255) {
			for (size_t j = 0; j < 3; j++) {
				pixels[i * 4 + j] = byte((pixels[i * 4 + j] * alpha) / 255);
			}
		}
	}

	texture_dimensions.x = int(decoded->width);
	texture_dimensions.y = int(decoded->height);

	return GenerateTexture({pixels, pixel_count * 4}, texture_dimensions);
}

Rml::TextureHandle RenderInterface_VK::GenerateTexture(Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions) {
	RMLUI_ASSERT(source_data.data() && source_data.size() == size_t(source_dimensions.x * source_dimensions.y * 4));
	Rml::String source_name = "generated-texture";
	return CreateTexture(source_data, source_dimensions, source_name);
}

/*
    How vulkan works with textures efficiently?

    You need to create buffer that has CPU memory accessibility it means it uses your RAM memory for storing data and it has only
   CPU visibility (RAM) After you create buffer that has GPU memory accessibility it means it uses by your video hardware and it
   has only VRAM (Video RAM) visibility

    So you copy data to CPU_buffer and after you copy that thing to GPU_buffer, but delete CPU_buffer

    So it means you "uploaded" data to GPU

    Again, you need to "write" data into CPU buffer after you need to copy that data from buffer to GPU buffer and after that
   buffer go to GPU.

    RAW_POINTER_DATA_BYTES_LITERALLY->COPY_TO->CPU->COPY_TO->GPU->Releasing_CPU <= that's how works uploading textures in Vulkan
   if you want to have efficient handling otherwise it is cpu_to_gpu visibility and it means you create only ONE buffer that is
   accessible for CPU and for GPU, but it will cause the worst performance...
*/
Rml::TextureHandle
    RenderInterface_VK::CreateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions, const Rml::String& name) {
	RMLUI_ZoneScopedN("Vulkan - GenerateTexture");

	RMLUI_VK_ASSERTMSG(!source.empty(), "you pushed not valid data for copying to buffer");
	RMLUI_VK_ASSERTMSG(m_p_allocator, "you have to initialize Vma Allocator for this method");
	(void)name;

	int width = dimensions.x;
	int height = dimensions.y;

	if (source.empty() || width <= 0 || height <= 0 || !m_p_allocator) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "Invalid RmlUi texture upload request.");
		return {};
	}

	VkDeviceSize image_size = source.size();
	VkFormat format = VkFormat::VK_FORMAT_R8G8B8A8_UNORM;

	buffer_data_t cpu_buffer = CreateResource_StagingBuffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	if (!cpu_buffer.m_p_vk_buffer || !cpu_buffer.m_p_vma_allocation) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create the RmlUi texture staging buffer.");
		return {};
	}

	void* data = nullptr;
	VkResult status = vmaMapMemory(m_p_allocator, cpu_buffer.m_p_vma_allocation, &data);
	if (status != VK_SUCCESS || !data) {
		DestroyResource_StagingBuffer(cpu_buffer);
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to map the RmlUi texture staging buffer.");
		return {};
	}
	memcpy(data, source.data(), static_cast<size_t>(image_size));
	vmaFlushAllocation(m_p_allocator, cpu_buffer.m_p_vma_allocation, 0, image_size);
	vmaUnmapMemory(m_p_allocator, cpu_buffer.m_p_vma_allocation);

	VkExtent3D extent_image = {};
	extent_image.width = static_cast<uint32_t>(width);
	extent_image.height = static_cast<uint32_t>(height);
	extent_image.depth = 1;

	auto texture = std::make_unique<texture_data_t>();
	auto* p_texture = texture.get();

	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.pNext = nullptr;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent = extent_image;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo info_allocation = {};
	info_allocation.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkImage p_image = nullptr;
	VmaAllocation p_allocation = nullptr;

	VmaAllocationInfo info_stats = {};
	status = vmaCreateImage(m_p_allocator, &info, &info_allocation, &p_image, &p_allocation, &info_stats);
	if (status != VK_SUCCESS) {
		DestroyResource_StagingBuffer(cpu_buffer);
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create an RmlUi texture image.");
		return {};
	}

#ifdef RMLUI_VK_DEBUG
	Rml::Log::Message(
	    Rml::Log::LT_DEBUG,
	    "Created texture '%s' [%dx%d, %s]",
	    name.c_str(),
	    dimensions.x,
	    dimensions.y,
	    FormatByteSize(info_stats.size).c_str()
	);
#endif

	p_texture->m_p_vk_image = p_image;
	p_texture->m_p_vma_allocation = p_allocation;

#ifdef RMLUI_VK_DEBUG
	vmaSetAllocationName(m_p_allocator, p_allocation, name.c_str());
#endif

	/*
	 * So Vulkan works only through VkCommandBuffer, it is for remembering API commands what you want to call from GPU
	 * So on CPU side you need to create a scope that consists of two things
	 * vkBeginCommandBuffer
	 * ... <= here your commands what you want to place into your command buffer and send it to GPU through vkQueueSubmit function
	 * vkEndCommandBuffer
	 *
	 * So commands start to work ONLY when you called the vkQueueSubmit otherwise you just "place" commands into your command buffer
	 * but you didn't issue any thing in order to start the work on GPU side. ALWAYS remember that just sumbit means execute async
	 * mode, so you have to wait operations before they exeecute fully otherwise you will get some errors or write/read concurrent
	 * state and all other stuff, vulkan validation will notify you :) (in most cases)
	 *
	 * BUT you need always sync what you have done when you called your vkQueueSubmit function, so it is wait method, but generally
	 * you can create another queue and isolate all stuff tbh
	 *
	 * So understing these principles you understand how to work with API and your GPU
	 *
	 * There's nothing hard, but it makes all stuff on programmer side if you remember OpenGL and how it was easy to load texture
	 * upload it and create buffers and it In OpenGL all stuff is handled by driver and other things, not a programmer definitely
	 *
	 * What we do here? We need to change the layout of our image. it means where we want to use it. So in our case we want to see
	 * that this image will be in shaders Because the initial state of create object is VK_IMAGE_LAYOUT_UNDEFINED means you can't
	 * just pass that VkImage handle to your functions and wait that it comes to shaders for exmaple No it doesn't work like that
	 * you have to have the explicit states of your resource and where it goes
	 *
	 * In our case we want to see in our pixel shader so we need to change transfer into this flag
	 * VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, because we want to copy so it means some transfer thing, but after we say it goes to
	 * pixel after our copying operation
	 */
	m_upload_manager.UploadToGPU([p_image, extent_image, cpu_buffer](VkCommandBuffer p_cmd) {
		VkImageSubresourceRange range = {};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.baseArrayLayer = 0;
		range.levelCount = 1;
		range.layerCount = 1;

		VkImageMemoryBarrier info_barrier = {};
		info_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		info_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		info_barrier.image = p_image;
		info_barrier.subresourceRange = range;
		info_barrier.srcAccessMask = 0;
		info_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(
		    p_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &info_barrier
		);

		VkBufferImageCopy region = {};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;

		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = extent_image;

		vkCmdCopyBufferToImage(p_cmd, cpu_buffer.m_p_vk_buffer, p_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		VkImageMemoryBarrier info_barrier_shader_read = {};
		info_barrier_shader_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		info_barrier_shader_read.pNext = nullptr;
		info_barrier_shader_read.image = p_image;
		info_barrier_shader_read.subresourceRange = range;
		info_barrier_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		info_barrier_shader_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info_barrier_shader_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		info_barrier_shader_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(
		    p_cmd,
		    VK_PIPELINE_STAGE_TRANSFER_BIT,
		    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		    0,
		    0,
		    nullptr,
		    0,
		    nullptr,
		    1,
		    &info_barrier_shader_read
		);
	});

	DestroyResource_StagingBuffer(cpu_buffer);

	VkImageViewCreateInfo info_image_view = {};
	info_image_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info_image_view.pNext = nullptr;
	info_image_view.image = p_texture->m_p_vk_image;
	info_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info_image_view.format = format;
	info_image_view.subresourceRange.baseMipLevel = 0;
	info_image_view.subresourceRange.levelCount = 1;
	info_image_view.subresourceRange.baseArrayLayer = 0;
	info_image_view.subresourceRange.layerCount = 1;
	info_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageView p_image_view = nullptr;
	status = vkCreateImageView(m_p_device, &info_image_view, nullptr, &p_image_view);
	if (status != VK_SUCCESS) {
		Destroy_Texture(*p_texture);
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create an RmlUi texture image view.");
		return {};
	}

	p_texture->m_p_vk_image_view = p_image_view;
	p_texture->m_p_vk_sampler = m_p_sampler_linear;

	auto owner = std::shared_ptr<texture_data_t>(texture.release(), [this](texture_data_t* resource) {
		Destroy_Texture(*resource);
		delete resource;
	});
	texture_data_t* handle = owner.get();
	m_live_resources.emplace(handle, std::move(owner));
	return reinterpret_cast<Rml::TextureHandle>(handle);
}

void RenderInterface_VK::ReleaseTexture(Rml::TextureHandle texture_handle) {
	ReleaseResource(reinterpret_cast<const void*>(texture_handle));
}

void RenderInterface_VK::SetTransform(const Rml::Matrix4f* transform) {
	m_user_data_for_vertex_shader.m_transform = m_projection * (transform ? *transform : Rml::Matrix4f::Identity());
}

// -- toast-engine adaptation: frame flow ----------------------------------------------------------
// The upstream backend acquired a swapchain image here and submitted/presented in EndFrame.
// In toast the engine renderer owns frame pacing; the UI records secondary command buffers on the
// main thread and the render thread executes them, so this section is fully rewritten.

std::shared_ptr<const void> RenderInterface_VK::OnFrameBegin(uint32_t frame_index) {
	m_frame_index = frame_index % kFramesInFlight;

	auto guard = m_command_buffer_ring.AcquireSlot(m_memory_pool);

	++m_pool_frame;
	m_p_current_pool = nullptr;
	RetireUnusedPools();
	for (auto& pool : m_pools) {
		pool->m_outputs_used = 0;
	}

	return guard;
}

VkCommandBuffer RenderInterface_VK::BeginRecording(VkExtent2D extent) {
	ZoneScopedN("UI context recording");
	RMLUI_VK_ASSERTMSG(m_p_current_command_buffer == nullptr, "BeginRecording called twice without EndRecording");
	if (m_p_current_command_buffer != nullptr || extent.width == 0 || extent.height == 0 || !m_p_device) {
		Rml::Log::Message(
		    Rml::Log::LT_ERROR, "BeginRecording rejected invalid UI context extent or device (%ux%u).", extent.width, extent.height
		);
		return nullptr;
	}

	m_p_current_command_buffer = m_command_buffer_ring.GetNextSecondaryBuffer();
	if (!m_p_current_command_buffer) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "vkAllocateCommandBuffers failed for a UI context command buffer.");
		return nullptr;
	}

	// Plain secondary buffer
	// The backend opens its own dynamic rendering scopes inside so the layer stack and filter passes can switch
	// render targets freely
	VkCommandBufferInheritanceInfo info_inheritance = {};
	info_inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;

	VkCommandBufferBeginInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	info.pInheritanceInfo = &info_inheritance;
	info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

	auto status = vkBeginCommandBuffer(m_p_current_command_buffer, &info);
	if (status != VK_SUCCESS) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "vkBeginCommandBuffer failed for UI context recording (VkResult=%d).", int(status));
		m_p_current_command_buffer = nullptr;
		return nullptr;
	}

	SetViewport(static_cast<int>(extent.width), static_cast<int>(extent.height));

	m_is_use_scissor_specified = false;
	m_is_apply_to_regular_geometry_stencil = false;
	m_is_use_stencil_pipeline = false;
	m_stencil_test_value = 1;

	LayerPool& pool = AcquirePool(extent);
	m_p_current_pool = &pool;
	if (!IsValidEffectImage(pool.m_stencil)) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "UI context recording aborted because stencil image creation failed.");
		vkEndCommandBuffer(m_p_current_command_buffer);
		m_p_current_command_buffer = nullptr;
		m_p_current_pool = nullptr;
		return nullptr;
	}
	m_command_buffer_ring.AddCurrentSlotGeneration(pool.m_generation);

	if (pool.m_outputs_used == pool.m_outputs.size()) {
		pool.m_outputs.push_back(CreateEffectImage(
		    pool.m_extent,
		    m_color_attachment_format,
		    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		));
	}
	effect_image_t* p_output = &pool.m_outputs[pool.m_outputs_used++];
	if (!IsValidEffectImage(*p_output)) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "UI context recording aborted because output image creation failed.");
		pool.m_outputs_used--;
		if (pool.m_outputs_used < pool.m_outputs.size()) {
			pool.m_outputs.erase(pool.m_outputs.begin() + pool.m_outputs_used);
		}
		vkEndCommandBuffer(m_p_current_command_buffer);
		m_p_current_command_buffer = nullptr;
		m_p_current_pool = nullptr;
		return nullptr;
	}

	m_layer_stack.clear();
	m_layer_stack.push_back(p_output);
	m_stack_layers_used = 0;

	BeginLayerScope(*p_output, true, true);
	if (!m_scope_active) {
		vkEndCommandBuffer(m_p_current_command_buffer);
		m_p_current_command_buffer = nullptr;
		m_p_current_pool = nullptr;
		m_layer_stack.clear();
		return nullptr;
	}

	return m_p_current_command_buffer;
}

VkCommandBuffer RenderInterface_VK::EndRecording() {
	ZoneScopedN("UI context recording end");
	RMLUI_VK_ASSERTMSG(m_p_current_command_buffer, "EndRecording called without BeginRecording");
	RMLUI_VK_ASSERTMSG(m_layer_stack.size() == 1, "layer stack not empty at EndRecording");

	if (!m_p_current_command_buffer || m_layer_stack.size() != 1 || !m_scope_active) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "EndRecording rejected invalid UI recording state.");
		return nullptr;
	}
	EndScope();

	effect_image_t* p_output = m_layer_stack.front();
	TransitionEffectImage(*p_output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_p_last_output_view = p_output->m_p_view;
	m_layer_stack.clear();

	VkCommandBuffer p_recorded = m_p_current_command_buffer;

	auto status = vkEndCommandBuffer(p_recorded);
	if (status != VK_SUCCESS) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "vkEndCommandBuffer failed for UI context recording (VkResult=%d).", int(status));
		p_recorded = nullptr;
	}

	m_p_current_command_buffer = nullptr;
	m_p_current_pool = nullptr;
	return p_recorded;
}

void RenderInterface_VK::SetViewport(int width, int height) {
	if (width > 0 && height > 0) {
		m_width = width;
		m_height = height;
	}

	m_viewport.x = 0.0f;
	m_viewport.y = 0.0f;
	m_viewport.width = static_cast<float>(m_width);
	m_viewport.height = static_cast<float>(m_height);
	m_viewport.minDepth = 0.0f;
	m_viewport.maxDepth = 1.0f;

	m_scissor_original.offset = {0, 0};
	m_scissor_original.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};
	m_scissor = m_scissor_original;

	m_projection =
	    Rml::Matrix4f::ProjectOrtho(0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, -10000, 10000);

	// Flip the Y axis for Vulkan clip space
	Rml::Matrix4f correction_matrix;
	correction_matrix.SetColumns(
	    Rml::Vector4f(1.0f, 0.0f, 0.0f, 0.0f),
	    Rml::Vector4f(0.0f, -1.0f, 0.0f, 0.0f),
	    Rml::Vector4f(0.0f, 0.0f, 0.5f, 0.0f),
	    Rml::Vector4f(0.0f, 0.0f, 0.5f, 1.0f)
	);

	m_projection = correction_matrix * m_projection;

	SetTransform(nullptr);
}

bool RenderInterface_VK::Initialize(
    VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family_index, VkQueue queue,
    VkFormat color_format, VkFormat depth_stencil_format, std::mutex* submit_mutex
) {
	RMLUI_ZoneScopedN("Vulkan - Initialize");
	RMLUI_VK_ASSERTMSG(instance && physical_device && device && queue, "you must pass valid Vulkan handles from the engine");

	m_p_instance = instance;
	m_p_physical_device = physical_device;
	m_p_device = device;
	m_queue_index_graphics = queue_family_index;
	m_color_attachment_format = color_format;
	m_depth_stencil_attachment_format = depth_stencil_format;
	m_p_submit_mutex = submit_mutex;

	VmaVulkanFunctions vulkan_functions = {};
	vulkan_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vulkan_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

	VmaAllocatorCreateInfo info_allocator = {};
	info_allocator.vulkanApiVersion = VK_API_VERSION_1_3;
	info_allocator.device = m_p_device;
	info_allocator.instance = m_p_instance;
	info_allocator.physicalDevice = m_p_physical_device;
	info_allocator.pVulkanFunctions = &vulkan_functions;

	auto status = vmaCreateAllocator(&info_allocator, &m_p_allocator);
	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vmaCreateAllocator");

	Initialize_Resources_Toast(queue);

	return true;
}

void RenderInterface_VK::Shutdown() {
	RMLUI_ZoneScopedN("Vulkan - Shutdown");

	// The device is shared with the whole engine; the UI system shuts down while the renderer is
	// stopped so waiting idle here is safe
	auto status = vkDeviceWaitIdle(m_p_device);
	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "you must have a valid status here");

	DestroyEffectResources();
	Destroy_Pipelines();
	Destroy_Resources();

	vmaDestroyAllocator(m_p_allocator);
	m_p_allocator = nullptr;
}

void RenderInterface_VK::Initialize_Resources_Toast(VkQueue queue) noexcept {
	m_command_buffer_ring.Initialize(m_p_device, m_queue_index_graphics);

	VkPhysicalDeviceProperties physical_device_properties = {};
	vkGetPhysicalDeviceProperties(m_p_physical_device, &physical_device_properties);

	const VkDeviceSize min_buffer_alignment = physical_device_properties.limits.minUniformBufferOffsetAlignment;
	m_memory_pool.Initialize(kVideoMemoryForAllocation, min_buffer_alignment, m_p_allocator, m_p_device);

	m_upload_manager.Initialize(m_p_device, queue, m_queue_index_graphics, m_p_submit_mutex);
	m_manager_descriptors.Initialize(m_p_device, 512, 1024, 64, 64);

	CreateShaders();
	CreateDescriptorSetLayout();
	CreatePipelineLayout();
	CreateSamplers();
	CreateDescriptorSets();
	Create_Pipelines();
	CreateEffectResources();
}

void RenderInterface_VK::Destroy_Resources() noexcept {
	m_command_buffer_ring.Shutdown(m_memory_pool);
	m_live_resources.clear();
	m_upload_manager.Shutdown();

	if (m_p_descriptor_set) {
		m_manager_descriptors.Free_Descriptors(m_p_device, &m_p_descriptor_set);
	}

	vkDestroyDescriptorSetLayout(m_p_device, m_p_descriptor_set_layout_vertex_transform, nullptr);
	vkDestroyDescriptorSetLayout(m_p_device, m_p_descriptor_set_layout_texture, nullptr);

	vkDestroyPipelineLayout(m_p_device, m_p_pipeline_layout, nullptr);

	for (const auto& p_module : m_shaders) {
		vkDestroyShaderModule(m_p_device, p_module, nullptr);
	}

	DestroySamplers();
	m_memory_pool.Shutdown();

	m_manager_descriptors.Shutdown(m_p_device);
}

void RenderInterface_VK::CreateShaders() noexcept {
	RMLUI_VK_ASSERTMSG(m_p_device, "[Vulkan] you must initialize VkDevice before calling this method");

	struct shader_data_t {
		const uint32_t* m_data;
		size_t m_data_size;
		VkShaderStageFlagBits m_shader_type;
	};

	const Rml::Vector<shader_data_t> shaders = {
	  {	      reinterpret_cast<const uint32_t*>(shader_vert),         sizeof(shader_vert),   VK_SHADER_STAGE_VERTEX_BIT},
	  {  reinterpret_cast<const uint32_t*>(shader_frag_color),   sizeof(shader_frag_color), VK_SHADER_STAGE_FRAGMENT_BIT},
	  {reinterpret_cast<const uint32_t*>(shader_frag_texture), sizeof(shader_frag_texture), VK_SHADER_STAGE_FRAGMENT_BIT},
	};

	for (const shader_data_t& shader_data : shaders) {
		VkShaderModuleCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		info.pCode = shader_data.m_data;
		info.codeSize = shader_data.m_data_size;

		VkShaderModule p_module = nullptr;
		VkResult status = vkCreateShaderModule(m_p_device, &info, nullptr, &p_module);

		RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "[Vulkan] failed to vkCreateShaderModule");

		m_shaders.push_back(p_module);
	}
}

void RenderInterface_VK::CreateDescriptorSetLayout() noexcept {
	RMLUI_VK_ASSERTMSG(m_p_device, "[Vulkan] you must initialize VkDevice before calling this method");
	RMLUI_VK_ASSERTMSG(
	    !m_p_descriptor_set_layout_vertex_transform && !m_p_descriptor_set_layout_texture, "[Vulkan] Already initialized"
	);

	{
		VkDescriptorSetLayoutBinding binding_for_vertex_transform = {};
		binding_for_vertex_transform.binding = 1;
		binding_for_vertex_transform.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		binding_for_vertex_transform.descriptorCount = 1;
		binding_for_vertex_transform.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		VkDescriptorSetLayoutCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.pBindings = &binding_for_vertex_transform;
		info.bindingCount = 1;

		VkResult status = vkCreateDescriptorSetLayout(m_p_device, &info, nullptr, &m_p_descriptor_set_layout_vertex_transform);
		RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "[Vulkan] failed to vkCreateDescriptorSetLayout");
	}

	{
		VkDescriptorSetLayoutBinding binding_for_fragment_texture = {};
		binding_for_fragment_texture.binding = 2;
		binding_for_fragment_texture.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding_for_fragment_texture.descriptorCount = 1;
		binding_for_fragment_texture.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.pBindings = &binding_for_fragment_texture;
		info.bindingCount = 1;

		VkResult status = vkCreateDescriptorSetLayout(m_p_device, &info, nullptr, &m_p_descriptor_set_layout_texture);
		RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "[Vulkan] failed to vkCreateDescriptorSetLayout");
	}
}

void RenderInterface_VK::CreatePipelineLayout() noexcept {
	RMLUI_VK_ASSERTMSG(
	    m_p_descriptor_set_layout_vertex_transform, "[Vulkan] You must initialize VkDescriptorSetLayout before calling this method"
	);
	RMLUI_VK_ASSERTMSG(
	    m_p_descriptor_set_layout_texture,
	    "[Vulkan] you must initialize VkDescriptorSetLayout for textures before calling this method!"
	);
	RMLUI_VK_ASSERTMSG(m_p_device, "[Vulkan] you must initialize VkDevice before calling this method");

	VkDescriptorSetLayout p_layouts[] = {m_p_descriptor_set_layout_vertex_transform, m_p_descriptor_set_layout_texture};

	VkPipelineLayoutCreateInfo info = {};

	info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	info.pNext = nullptr;
	info.pSetLayouts = p_layouts;
	info.setLayoutCount = 2;

	auto status = vkCreatePipelineLayout(m_p_device, &info, nullptr, &m_p_pipeline_layout);

	RMLUI_VK_ASSERTMSG(status == VK_SUCCESS, "[Vulkan] failed to vkCreatePipelineLayout");
}

void RenderInterface_VK::CreateDescriptorSets() noexcept {
	RMLUI_VK_ASSERTMSG(m_p_device, "[Vulkan] you have to initialize your VkDevice before calling this method");
	RMLUI_VK_ASSERTMSG(
	    m_p_descriptor_set_layout_vertex_transform,
	    "[Vulkan] you have to initialize your VkDescriptorSetLayout before calling this method"
	);

	if (!m_manager_descriptors.Alloc_Descriptor(m_p_device, &m_p_descriptor_set_layout_vertex_transform, &m_p_descriptor_set)) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to allocate the RmlUi transform descriptor set.");
		return;
	}
	m_memory_pool.SetDescriptorSet(
	    1, sizeof(shader_vertex_user_data_t), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, m_p_descriptor_set
	);
}

void RenderInterface_VK::CreateSamplers() noexcept {
	VkSamplerCreateInfo info = {};

	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.pNext = nullptr;
	info.magFilter = VK_FILTER_LINEAR;
	info.minFilter = VK_FILTER_LINEAR;
	info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	vkCreateSampler(m_p_device, &info, nullptr, &m_p_sampler_linear);
}

void RenderInterface_VK::Create_Pipelines() noexcept {
	RMLUI_VK_ASSERTMSG(m_p_pipeline_layout, "must be initialized");
	RMLUI_VK_ASSERTMSG(
	    m_color_attachment_format != VK_FORMAT_UNDEFINED, "attachment formats must be set before creating pipelines"
	);

	// toast-engine adaptation: pipelines target the UI pass's dynamic rendering attachments
	// instead of an owned render pass
	VkPipelineRenderingCreateInfo info_rendering = {};
	info_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	info_rendering.colorAttachmentCount = 1;
	info_rendering.pColorAttachmentFormats = &m_color_attachment_format;
	info_rendering.depthAttachmentFormat =
	    m_depth_stencil_attachment_format == VK_FORMAT_S8_UINT ? VK_FORMAT_UNDEFINED : m_depth_stencil_attachment_format;
	info_rendering.stencilAttachmentFormat = m_depth_stencil_attachment_format;

	VkPipelineInputAssemblyStateCreateInfo info_assembly_state = {};
	info_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	info_assembly_state.pNext = nullptr;
	info_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	info_assembly_state.primitiveRestartEnable = VK_FALSE;
	info_assembly_state.flags = 0;

	VkPipelineRasterizationStateCreateInfo info_raster_state = {};
	info_raster_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	info_raster_state.pNext = nullptr;
	info_raster_state.polygonMode = VK_POLYGON_MODE_FILL;
	info_raster_state.cullMode = VK_CULL_MODE_NONE;
	info_raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	info_raster_state.rasterizerDiscardEnable = VK_FALSE;
	info_raster_state.depthBiasEnable = VK_FALSE;
	info_raster_state.lineWidth = 1.0f;

	VkPipelineColorBlendAttachmentState info_color_blend_att = {};
	info_color_blend_att.colorWriteMask = 0xf;
	info_color_blend_att.blendEnable = VK_TRUE;
	info_color_blend_att.srcColorBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
	info_color_blend_att.dstColorBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	info_color_blend_att.colorBlendOp = VkBlendOp::VK_BLEND_OP_ADD;
	info_color_blend_att.srcAlphaBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
	info_color_blend_att.dstAlphaBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	info_color_blend_att.alphaBlendOp = VkBlendOp::VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo info_color_blend_state = {};
	info_color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	info_color_blend_state.pNext = nullptr;
	info_color_blend_state.attachmentCount = 1;
	info_color_blend_state.pAttachments = &info_color_blend_att;

	VkPipelineDepthStencilStateCreateInfo info_depth = {};
	info_depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	info_depth.pNext = nullptr;
	info_depth.depthTestEnable = VK_FALSE;
	info_depth.depthWriteEnable = VK_TRUE;
	info_depth.depthBoundsTestEnable = VK_FALSE;
	info_depth.maxDepthBounds = 1.0f;

	info_depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

	info_depth.stencilTestEnable = VK_TRUE;
	info_depth.back.compareOp = VK_COMPARE_OP_ALWAYS;
	info_depth.back.failOp = VK_STENCIL_OP_KEEP;
	info_depth.back.depthFailOp = VK_STENCIL_OP_KEEP;
	info_depth.back.passOp = VK_STENCIL_OP_KEEP;
	info_depth.back.compareMask = 0xff;
	info_depth.back.writeMask = 0xff;
	info_depth.back.reference = 1;
	info_depth.front = info_depth.back;

	VkPipelineViewportStateCreateInfo info_viewport = {};
	info_viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	info_viewport.pNext = nullptr;
	info_viewport.viewportCount = 1;
	info_viewport.scissorCount = 1;
	info_viewport.flags = 0;

	VkPipelineMultisampleStateCreateInfo info_multisample = {};
	info_multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	info_multisample.pNext = nullptr;
	info_multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	info_multisample.flags = 0;

	// stencil reference is dynamic
	Rml::Array<VkDynamicState, 3> dynamicStateEnables = {
	  VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE
	};

	VkPipelineDynamicStateCreateInfo info_dynamic_state = {};
	info_dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	info_dynamic_state.pNext = nullptr;
	info_dynamic_state.pDynamicStates = dynamicStateEnables.data();
	info_dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
	info_dynamic_state.flags = 0;

	Rml::Array<VkPipelineShaderStageCreateInfo, 2> shaders_that_will_be_used_in_pipeline;

	VkPipelineShaderStageCreateInfo info_shader = {};
	info_shader.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info_shader.pNext = nullptr;
	info_shader.pName = "main";
	info_shader.stage = VK_SHADER_STAGE_VERTEX_BIT;
	info_shader.module = m_shaders[static_cast<int>(shader_id_t::Vertex)];

	shaders_that_will_be_used_in_pipeline[0] = info_shader;

	info_shader.module = m_shaders[static_cast<int>(shader_id_t::Fragment_WithTextures)];
	info_shader.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

	shaders_that_will_be_used_in_pipeline[1] = info_shader;

	VkPipelineVertexInputStateCreateInfo info_vertex = {};
	info_vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	info_vertex.pNext = nullptr;
	info_vertex.flags = 0;

	Rml::Array<VkVertexInputAttributeDescription, 3> info_shader_vertex_attributes;
	// describe info about our vertex and what is used in vertex shader as "layout(location = X) in"

	VkVertexInputBindingDescription info_vertex_input_binding = {};
	info_vertex_input_binding.binding = 0;
	info_vertex_input_binding.stride = sizeof(Rml::Vertex);
	info_vertex_input_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	info_shader_vertex_attributes[0].binding = 0;
	info_shader_vertex_attributes[0].location = 0;
	info_shader_vertex_attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
	info_shader_vertex_attributes[0].offset = offsetof(Rml::Vertex, position);

	info_shader_vertex_attributes[1].binding = 0;
	info_shader_vertex_attributes[1].location = 1;
	info_shader_vertex_attributes[1].format = VK_FORMAT_R8G8B8A8_UNORM;
	info_shader_vertex_attributes[1].offset = offsetof(Rml::Vertex, colour);

	info_shader_vertex_attributes[2].binding = 0;
	info_shader_vertex_attributes[2].location = 2;
	info_shader_vertex_attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
	info_shader_vertex_attributes[2].offset = offsetof(Rml::Vertex, tex_coord);

	info_vertex.pVertexAttributeDescriptions = info_shader_vertex_attributes.data();
	info_vertex.vertexAttributeDescriptionCount = static_cast<uint32_t>(info_shader_vertex_attributes.size());
	info_vertex.pVertexBindingDescriptions = &info_vertex_input_binding;
	info_vertex.vertexBindingDescriptionCount = 1;

	VkGraphicsPipelineCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.pNext = &info_rendering;
	info.pInputAssemblyState = &info_assembly_state;
	info.pRasterizationState = &info_raster_state;
	info.pColorBlendState = &info_color_blend_state;
	info.pMultisampleState = &info_multisample;
	info.pViewportState = &info_viewport;
	info.pDepthStencilState = &info_depth;
	info.pDynamicState = &info_dynamic_state;
	info.stageCount = static_cast<uint32_t>(shaders_that_will_be_used_in_pipeline.size());
	info.pStages = shaders_that_will_be_used_in_pipeline.data();
	info.pVertexInputState = &info_vertex;
	info.layout = m_p_pipeline_layout;
	info.renderPass = VK_NULL_HANDLE;
	info.subpass = 0;

	auto status = vkCreateGraphicsPipelines(m_p_device, nullptr, 1, &info, nullptr, &m_p_pipeline_with_textures);
	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkCreateGraphicsPipelines");

	info_depth.back.passOp = VK_STENCIL_OP_KEEP;
	info_depth.back.failOp = VK_STENCIL_OP_KEEP;
	info_depth.back.depthFailOp = VK_STENCIL_OP_KEEP;
	info_depth.back.compareOp = VK_COMPARE_OP_EQUAL;
	info_depth.back.compareMask = 0xff;
	info_depth.back.writeMask = 0xff;
	info_depth.back.reference = 1;
	info_depth.front = info_depth.back;

	status = vkCreateGraphicsPipelines(
	    m_p_device, nullptr, 1, &info, nullptr, &m_p_pipeline_stencil_for_regular_geometry_that_applied_to_region_with_textures
	);
	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkCreateGraphicsPipelines");

	info_shader.module = m_shaders[static_cast<int>(shader_id_t::Fragment_WithoutTextures)];
	shaders_that_will_be_used_in_pipeline[1] = info_shader;
	info_depth.back.compareOp = VK_COMPARE_OP_ALWAYS;
	info_depth.back.failOp = VK_STENCIL_OP_KEEP;
	info_depth.back.depthFailOp = VK_STENCIL_OP_KEEP;
	info_depth.back.passOp = VK_STENCIL_OP_KEEP;
	info_depth.back.compareMask = 0xff;
	info_depth.back.writeMask = 0xff;
	info_depth.back.reference = 1;
	info_depth.front = info_depth.back;

	status = vkCreateGraphicsPipelines(m_p_device, nullptr, 1, &info, nullptr, &m_p_pipeline_without_textures);
	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkCreateGraphicsPipelines");

	info_depth.back.passOp = VK_STENCIL_OP_KEEP;
	info_depth.back.failOp = VK_STENCIL_OP_KEEP;
	info_depth.back.depthFailOp = VK_STENCIL_OP_KEEP;
	info_depth.back.compareOp = VK_COMPARE_OP_EQUAL;
	info_depth.back.compareMask = 0xff;
	info_depth.back.writeMask = 0xff;
	info_depth.back.reference = 1;
	info_depth.front = info_depth.back;

	status = vkCreateGraphicsPipelines(
	    m_p_device, nullptr, 1, &info, nullptr, &m_p_pipeline_stencil_for_regular_geometry_that_applied_to_region_without_textures
	);
	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkCreateGraphicsPipelines");

	info_color_blend_att.colorWriteMask = 0x0;
	info_depth.back.passOp = VK_STENCIL_OP_REPLACE;
	info_depth.back.failOp = VK_STENCIL_OP_KEEP;
	info_depth.back.depthFailOp = VK_STENCIL_OP_KEEP;
	info_depth.back.compareOp = VK_COMPARE_OP_ALWAYS;
	info_depth.back.compareMask = 0xff;
	info_depth.back.writeMask = 0xff;
	info_depth.back.reference = 1;
	info_depth.front = info_depth.back;

	status = vkCreateGraphicsPipelines(
	    m_p_device, nullptr, 1, &info, nullptr, &m_p_pipeline_stencil_for_region_where_geometry_will_be_drawn
	);
	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkCreateGraphicsPipelines");

	// increment variant for ClipMaskOperation::Intersect
	info_depth.back.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
	info_depth.front = info_depth.back;

	status = vkCreateGraphicsPipelines(m_p_device, nullptr, 1, &info, nullptr, &m_p_pipeline_clip_write_incr);
	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkCreateGraphicsPipelines");
}

RenderInterface_VK::buffer_data_t
    RenderInterface_VK::CreateResource_StagingBuffer(VkDeviceSize size, VkBufferUsageFlags flags) noexcept {
	VkBufferCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.pNext = nullptr;
	info.size = size;
	info.usage = flags;

	VmaAllocationCreateInfo info_allocation = {};
	info_allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	info_allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

	VkBuffer p_buffer = nullptr;
	VmaAllocation p_allocation = nullptr;
	VmaAllocationInfo info_stats = {};

	VkResult status = vmaCreateBuffer(m_p_allocator, &info, &info_allocation, &p_buffer, &p_allocation, &info_stats);
	if (status != VK_SUCCESS) {
		RMLUI_VK_ASSERTMSG(false, "failed to vmaCreateBuffer");
		return {};
	}

#ifdef RMLUI_VK_DEBUG
	Rml::Log::Message(Rml::Log::LT_DEBUG, "Allocated buffer [%s]", FormatByteSize(info_stats.size).c_str());
#endif

	buffer_data_t result = {};
	result.m_p_vk_buffer = p_buffer;
	result.m_p_vma_allocation = p_allocation;

	return result;
}

void RenderInterface_VK::DestroyResource_StagingBuffer(const buffer_data_t& data) noexcept {
	if (m_p_allocator) {
		if (data.m_p_vk_buffer && data.m_p_vma_allocation) {
			vmaDestroyBuffer(m_p_allocator, data.m_p_vk_buffer, data.m_p_vma_allocation);
		}
	}
}

void RenderInterface_VK::Destroy_Texture(const texture_data_t& texture) noexcept {
	RMLUI_VK_ASSERTMSG(m_p_allocator, "you must have initialized VmaAllocator");
	RMLUI_VK_ASSERTMSG(m_p_device, "you must have initialized VkDevice");

	VkDescriptorSet p_set = texture.m_p_vk_descriptor_set;
	if (p_set) {
		m_manager_descriptors.Free_Descriptors(m_p_device, &p_set);
	}
	if (texture.m_p_vk_image_view) {
		vkDestroyImageView(m_p_device, texture.m_p_vk_image_view, nullptr);
	}
	if (texture.m_p_vk_image && texture.m_p_vma_allocation) {
		vmaDestroyImage(m_p_allocator, texture.m_p_vk_image, texture.m_p_vma_allocation);
	}
}

bool RenderInterface_VK::RetainResource(const void* resource) {
	if (!resource) {
		return false;
	}
	const auto it = m_live_resources.find(resource);
	if (it == m_live_resources.end()) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "Attempted to render a released or unknown RmlUi resource.");
		return false;
	}
	m_command_buffer_ring.AddCurrentSlotResource(it->second);
	return true;
}

void RenderInterface_VK::ReleaseResource(const void* resource) {
	if (resource) {
		m_live_resources.erase(resource);
	}
}

void RenderInterface_VK::Destroy_Pipelines() noexcept {
	RMLUI_VK_ASSERTMSG(m_p_device, "must exist here");

	vkDestroyPipeline(m_p_device, m_p_pipeline_with_textures, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_without_textures, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_stencil_for_region_where_geometry_will_be_drawn, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_stencil_for_regular_geometry_that_applied_to_region_with_textures, nullptr);
	vkDestroyPipeline(m_p_device, m_p_pipeline_stencil_for_regular_geometry_that_applied_to_region_without_textures, nullptr);
}

void RenderInterface_VK::DestroyDescriptorSets() noexcept { }

void RenderInterface_VK::DestroyPipelineLayout() noexcept { }

void RenderInterface_VK::DestroySamplers() noexcept {
	RMLUI_VK_ASSERTMSG(m_p_device, "must exist here");
	vkDestroySampler(m_p_device, m_p_sampler_linear, nullptr);
}

// -- toast-engine adaptation: secondary command buffer ring ---------------------------------------
// Upstream kept one primary command buffer per swapchain image. Here every panel context records
// its own secondary buffer per frame, so pools grow on demand and reset when their slot cycles.

RenderInterface_VK::CommandBufferRing::CommandBufferRing() : m_p_device {}, m_queue_index {}, m_p_current_slot {} { }

void RenderInterface_VK::CommandBufferRing::Initialize(VkDevice p_device, uint32_t queue_index_graphics) noexcept {
	RMLUI_VK_ASSERTMSG(p_device, "you can't pass an invalid VkDevice here");
	RMLUI_VK_ASSERTMSG(!m_p_device, "already initialized");

	m_p_device = p_device;
	m_queue_index = queue_index_graphics;

	// Create one slot per frame in flight
	for (uint32_t i = 0; i < kFramesInFlight; i++) {
		CreateSlot();
	}
}

void RenderInterface_VK::CommandBufferRing::Shutdown(MemoryPool& memory_pool) {
	for (auto& slot : m_slots) {
		for (const VmaVirtualAllocation allocation : slot->m_transient_allocations) {
			memory_pool.Free_Allocation(allocation);
		}
		slot->m_transient_allocations.clear();
		slot->m_resources.clear();

		if (!slot->m_command_buffers.empty()) {
			vkFreeCommandBuffers(
			    m_p_device, slot->m_command_pool, static_cast<uint32_t>(slot->m_command_buffers.size()), slot->m_command_buffers.data()
			);
		}
		vkDestroyCommandPool(m_p_device, slot->m_command_pool, nullptr);
	}

	m_slots.clear();
	m_p_current_slot = nullptr;
	m_p_device = nullptr;
}

RenderInterface_VK::CommandBufferRing::Slot& RenderInterface_VK::CommandBufferRing::CreateSlot() {
	auto slot = Rml::MakeUnique<Slot>();

	VkCommandPoolCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	info.pNext = nullptr;
	info.queueFamilyIndex = m_queue_index;
	info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

	VkResult status = vkCreateCommandPool(m_p_device, &info, nullptr, &slot->m_command_pool);
	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkCreateCommandPool");

	slot->m_guard = std::make_shared<const int>(0);

	m_slots.push_back(std::move(slot));
	return *m_slots.back();
}

std::shared_ptr<const void> RenderInterface_VK::CommandBufferRing::AcquireSlot(MemoryPool& memory_pool) {
	Slot* p_free = nullptr;
	for (auto& slot : m_slots) {
		// use_count == 1 means no references
		if (slot->m_guard.use_count() == 1) {
			p_free = slot.get();
			break;
		}
	}

	if (!p_free) {
		p_free = &CreateSlot();
	}

	for (const VmaVirtualAllocation allocation : p_free->m_transient_allocations) {
		memory_pool.Free_Allocation(allocation);
	}
	p_free->m_transient_allocations.clear();
	p_free->m_resources.clear();

	vkResetCommandPool(m_p_device, p_free->m_command_pool, 0);
	p_free->m_used = 0;
	p_free->m_generations.clear();
	m_p_current_slot = p_free;

	return p_free->m_guard;
}

VkCommandBuffer RenderInterface_VK::CommandBufferRing::GetNextSecondaryBuffer() {
	RMLUI_VK_ASSERTMSG(m_p_current_slot, "AcquireSlot must be called before recording");
	Slot& slot = *m_p_current_slot;

	if (slot.m_used == slot.m_command_buffers.size()) {
		VkCommandBufferAllocateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		info.pNext = nullptr;
		info.commandPool = slot.m_command_pool;
		info.commandBufferCount = 1;
		info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;

		VkCommandBuffer p_buffer = nullptr;
		VkResult status = vkAllocateCommandBuffers(m_p_device, &info, &p_buffer);
		RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkAllocateCommandBuffers");

		slot.m_command_buffers.push_back(p_buffer);
	}

	return slot.m_command_buffers[slot.m_used++];
}

void RenderInterface_VK::CommandBufferRing::AddCurrentSlotAllocation(VmaVirtualAllocation allocation) {
	RMLUI_VK_ASSERTMSG(m_p_current_slot, "AcquireSlot must be called first");
	RMLUI_VK_ASSERTMSG(allocation, "Cannot retain an empty transient allocation");
	m_p_current_slot->m_transient_allocations.push_back(allocation);
}

void RenderInterface_VK::CommandBufferRing::AddCurrentSlotResource(std::shared_ptr<const void> resource) {
	RMLUI_VK_ASSERTMSG(m_p_current_slot, "AcquireSlot must be called first");
	if (resource) {
		m_p_current_slot->m_resources.push_back(std::move(resource));
	}
}

RenderInterface_VK::MemoryPool::MemoryPool()
    : m_memory_total_size {},
      m_device_min_uniform_alignment {},
      m_p_data {},
      m_p_buffer {},
      m_p_buffer_alloc {},
      m_p_device {},
      m_p_vk_allocator {},
      m_p_block {} { }

RenderInterface_VK::MemoryPool::~MemoryPool() { }

void RenderInterface_VK::MemoryPool::Initialize(
    VkDeviceSize byte_size, VkDeviceSize device_min_uniform_alignment, VmaAllocator p_allocator, VkDevice p_device
) noexcept {
	RMLUI_VK_ASSERTMSG(byte_size > 0, "size must be valid");
	RMLUI_VK_ASSERTMSG(device_min_uniform_alignment > 0, "uniform alignment must be valid");
	RMLUI_VK_ASSERTMSG(p_device, "you must pass a valid VkDevice");
	RMLUI_VK_ASSERTMSG(p_allocator, "you must pass a valid VmaAllocator");

	m_p_device = p_device;
	m_p_vk_allocator = p_allocator;
	m_device_min_uniform_alignment = device_min_uniform_alignment;

#ifdef RMLUI_VK_DEBUG
	Rml::Log::Message(
	    Rml::Log::LT_DEBUG, "[Vulkan][Debug] the alignment for uniform buffer is: %zu", m_device_min_uniform_alignment
	);
#endif

	m_memory_total_size = AlignUp<VkDeviceSize>(static_cast<VkDeviceSize>(byte_size), m_device_min_uniform_alignment);

	VkBufferCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	info.size = m_memory_total_size;

	VmaAllocationCreateInfo info_alloc = {};

	auto p_commentary = "our pool buffer that manages all memory in vulkan (dynamic)";

	info_alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	info_alloc.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	info_alloc.pUserData = const_cast<char*>(p_commentary);

	VmaAllocationInfo info_stats = {};

	auto status = vmaCreateBuffer(m_p_vk_allocator, &info, &info_alloc, &m_p_buffer, &m_p_buffer_alloc, &info_stats);

	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vmaCreateBuffer");

	VmaVirtualBlockCreateInfo info_virtual_block = {};
	info_virtual_block.size = m_memory_total_size;

	status = vmaCreateVirtualBlock(&info_virtual_block, &m_p_block);

	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vmaCreateVirtualBlock");

#ifdef RMLUI_VK_DEBUG
	Rml::Log::Message(Rml::Log::LT_DEBUG, "[Vulkan][Debug] Allocated memory pool [%s]", FormatByteSize(info_stats.size).c_str());
#endif

	status = vmaMapMemory(m_p_vk_allocator, m_p_buffer_alloc, (void**)&m_p_data);

	RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vmaMapMemory");
}

void RenderInterface_VK::MemoryPool::Shutdown() noexcept {
	RMLUI_VK_ASSERTMSG(m_p_vk_allocator, "you must have a valid VmaAllocator");
	RMLUI_VK_ASSERTMSG(m_p_buffer, "you must allocate VkBuffer for deleting");
	RMLUI_VK_ASSERTMSG(m_p_buffer_alloc, "you must allocate VmaAllocation for deleting");

#ifdef RMLUI_VK_DEBUG
	Rml::Log::Message(
	    Rml::Log::LT_DEBUG, "[Vulkan][Debug] Destroyed memory pool [%s]", FormatByteSize(m_memory_total_size).c_str()
	);
#endif

	vmaUnmapMemory(m_p_vk_allocator, m_p_buffer_alloc);
	vmaDestroyVirtualBlock(m_p_block);
	vmaDestroyBuffer(m_p_vk_allocator, m_p_buffer, m_p_buffer_alloc);
	m_p_data = nullptr;
	m_p_block = nullptr;
	m_p_buffer = nullptr;
	m_p_buffer_alloc = nullptr;
}

bool RenderInterface_VK::MemoryPool::Alloc_GeneralBuffer(
    VkDeviceSize size, void** p_data, VkDescriptorBufferInfo* p_out, VmaVirtualAllocation* p_alloc
) noexcept {
	RMLUI_VK_ASSERTMSG(p_out, "you must pass a valid pointer");
	RMLUI_VK_ASSERTMSG(m_p_buffer, "you must have a valid VkBuffer");

	if (!p_data || !p_out || !p_alloc || !m_p_block || !m_p_data || size == 0 || *p_alloc != nullptr) {
		return false;
	}
	*p_data = nullptr;
	*p_out = {};

	size = AlignUp<VkDeviceSize>(static_cast<VkDeviceSize>(size), m_device_min_uniform_alignment);

	VkDeviceSize offset_memory {};

	VmaVirtualAllocationCreateInfo info = {};
	info.size = size;
	info.alignment = m_device_min_uniform_alignment;

	auto status = vmaVirtualAllocate(m_p_block, &info, p_alloc, &offset_memory);

	if (status != VK_SUCCESS) {
		*p_alloc = nullptr;
		return false;
	}

	*p_data = static_cast<void*>(m_p_data + offset_memory);

	p_out->buffer = m_p_buffer;
	p_out->offset = offset_memory;
	p_out->range = size;

	return true;
}

void RenderInterface_VK::MemoryPool::Flush(const VkDescriptorBufferInfo& range) noexcept {
	if (m_p_vk_allocator && m_p_buffer_alloc && range.buffer == m_p_buffer && range.range > 0) {
		vmaFlushAllocation(m_p_vk_allocator, m_p_buffer_alloc, range.offset, range.range);
	}
}

bool RenderInterface_VK::MemoryPool::Alloc_VertexBuffer(
    uint32_t number_of_elements, uint32_t stride_in_bytes, void** p_data, VkDescriptorBufferInfo* p_out,
    VmaVirtualAllocation* p_alloc
) noexcept {
	return Alloc_GeneralBuffer(number_of_elements * stride_in_bytes, p_data, p_out, p_alloc);
}

bool RenderInterface_VK::MemoryPool::Alloc_IndexBuffer(
    uint32_t number_of_elements, uint32_t stride_in_bytes, void** p_data, VkDescriptorBufferInfo* p_out,
    VmaVirtualAllocation* p_alloc
) noexcept {
	return Alloc_GeneralBuffer(number_of_elements * stride_in_bytes, p_data, p_out, p_alloc);
}

void RenderInterface_VK::MemoryPool::SetDescriptorSet(
    uint32_t binding_index, uint32_t size, VkDescriptorType descriptor_type, VkDescriptorSet p_set
) noexcept {
	RMLUI_VK_ASSERTMSG(m_p_device, "you must have a valid VkDevice here");
	RMLUI_VK_ASSERTMSG(p_set, "you must have a valid VkDescriptorSet here");
	RMLUI_VK_ASSERTMSG(m_p_buffer, "you must have a valid VkBuffer here");

	VkDescriptorBufferInfo info = {};

	info.buffer = m_p_buffer;
	info.offset = 0;
	info.range = size;

	VkWriteDescriptorSet info_write = {};

	info_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	info_write.pNext = nullptr;
	info_write.dstSet = p_set;
	info_write.descriptorCount = 1;
	info_write.descriptorType = descriptor_type;
	info_write.dstArrayElement = 0;
	info_write.dstBinding = binding_index;
	info_write.pBufferInfo = &info;

	vkUpdateDescriptorSets(m_p_device, 1, &info_write, 0, nullptr);
}

void RenderInterface_VK::MemoryPool::SetDescriptorSet(
    uint32_t binding_index, VkDescriptorBufferInfo* p_info, VkDescriptorType descriptor_type, VkDescriptorSet p_set
) noexcept {
	RMLUI_VK_ASSERTMSG(m_p_device, "you must have a valid VkDevice here");
	RMLUI_VK_ASSERTMSG(p_set, "you must have a valid VkDescriptorSet here");
	RMLUI_VK_ASSERTMSG(m_p_buffer, "you must have a valid VkBuffer here");
	RMLUI_VK_ASSERTMSG(p_info, "must be valid pointer");

	VkWriteDescriptorSet info_write = {};

	info_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	info_write.pNext = nullptr;
	info_write.dstSet = p_set;
	info_write.descriptorCount = 1;
	info_write.descriptorType = descriptor_type;
	info_write.dstArrayElement = 0;
	info_write.dstBinding = binding_index;
	info_write.pBufferInfo = p_info;

	vkUpdateDescriptorSets(m_p_device, 1, &info_write, 0, nullptr);
}

void RenderInterface_VK::MemoryPool::SetDescriptorSet(
    uint32_t binding_index, VkSampler p_sampler, VkImageLayout layout, VkImageView p_view, VkDescriptorType descriptor_type,
    VkDescriptorSet p_set
) noexcept {
	RMLUI_VK_ASSERTMSG(m_p_device, "you must have a valid VkDevice here");
	RMLUI_VK_ASSERTMSG(p_set, "you must have a valid VkDescriptorSet here");
	RMLUI_VK_ASSERTMSG(m_p_buffer, "you must have a valid VkBuffer here");
	RMLUI_VK_ASSERTMSG(p_view, "you must have a valid VkImageView");
	RMLUI_VK_ASSERTMSG(p_sampler, "you must have a valid VkSampler here");

	VkDescriptorImageInfo info = {};

	info.imageLayout = layout;
	info.imageView = p_view;
	info.sampler = p_sampler;

	VkWriteDescriptorSet info_write = {};

	info_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	info_write.pNext = nullptr;
	info_write.dstSet = p_set;
	info_write.descriptorCount = 1;
	info_write.descriptorType = descriptor_type;
	info_write.dstArrayElement = 0;
	info_write.dstBinding = binding_index;
	info_write.pImageInfo = &info;

	vkUpdateDescriptorSets(m_p_device, 1, &info_write, 0, nullptr);
}

void RenderInterface_VK::MemoryPool::Free_GeometryHandle(geometry_handle_t* p_valid_geometry_handle) noexcept {
	RMLUI_VK_ASSERTMSG(
	    p_valid_geometry_handle,
	    "you must pass a VALID pointer to geometry_handle_t, otherwise something is wrong and debug your code"
	);
	RMLUI_VK_ASSERTMSG(
	    p_valid_geometry_handle->m_p_vertex_allocation, "you must have a VALID pointer of VmaAllocation for vertex buffer"
	);
	RMLUI_VK_ASSERTMSG(
	    p_valid_geometry_handle->m_p_index_allocation, "you must have a VALID pointer of VmaAllocation for index buffer"
	);

	RMLUI_VK_ASSERTMSG(m_p_block, "you have to allocate the virtual block before do this operation...");

	vmaVirtualFree(m_p_block, p_valid_geometry_handle->m_p_vertex_allocation);
	vmaVirtualFree(m_p_block, p_valid_geometry_handle->m_p_index_allocation);

	p_valid_geometry_handle->m_p_vertex_allocation = nullptr;
	p_valid_geometry_handle->m_p_index_allocation = nullptr;
	p_valid_geometry_handle->m_num_indices = 0;
}

void RenderInterface_VK::MemoryPool::Free_Allocation(VmaVirtualAllocation allocation) noexcept {
	if (m_p_block && allocation) {
		vmaVirtualFree(m_p_block, allocation);
	}
}

void RenderInterface_VK::CommandBufferRing::AddCurrentSlotGeneration(uint64_t generation) {
	RMLUI_VK_ASSERTMSG(m_p_current_slot, "AcquireSlot must be called first");
	auto& generations = m_p_current_slot->m_generations;
	if (std::find(generations.begin(), generations.end(), generation) == generations.end()) {
		generations.push_back(generation);
	}
}

bool RenderInterface_VK::CommandBufferRing::IsGenerationReferenced(uint64_t generation) const {
	for (const auto& slot : m_slots) {
		if (slot->m_guard.use_count() <= 1) {
			continue;
		}
		if (std::find(slot->m_generations.begin(), slot->m_generations.end(), generation) != slot->m_generations.end()) {
			return true;
		}
	}
	return false;
}

// NOLINTEND
