/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 * https://github.com/mikke89/RmlUi — vendored at tag 6.2 and adapted for toast-engine,
 * see README.md in this directory for the list of local changes.
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2023 The RmlUi Team, and contributors
 *
 * Licensed under the MIT license, see the RmlUi repository for the full text.
 */

#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <mutex>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#ifdef RMLUI_DEBUG
#define RMLUI_VK_ASSERTMSG(statement, msg) RMLUI_ASSERTMSG(statement, msg)
#else
#define RMLUI_VK_ASSERTMSG(statement, msg) static_cast<void>(statement)
#endif

class RenderInterface_VK : public Rml::RenderInterface {
public:
	// Matches the engine renderer's frames-in-flight; per-frame resources cycle on this
	static constexpr uint32_t kFramesInFlight = 3;
	static constexpr VkDeviceSize kVideoMemoryForAllocation = 4 * 1024 * 1024;    // [bytes]

	RenderInterface_VK();
	~RenderInterface_VK();

	/// Borrows the engine's Vulkan handles; the backend owns only its own resources.
	/// @param submit_mutex guards graphics queue submission shared with the render thread
	bool Initialize(
	    VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family_index, VkQueue queue,
	    VkFormat color_format, VkFormat depth_stencil_format, std::mutex* submit_mutex
	);
	void Shutdown();

	/// Called once per built frame on the main thread; resets this frame's command pool
	/// and runs deferred destruction for resources retired kFramesInFlight frames ago
	void OnFrameBegin(uint32_t frame_index);

	/// Begins recording a secondary command buffer for one context render target.
	/// SetViewport() must have been called with the context dimensions beforehand
	VkCommandBuffer BeginRecording(VkExtent2D extent);
	/// Ends recording and returns the secondary command buffer for execution on the render thread
	VkCommandBuffer EndRecording();

	void SetViewport(int width, int height);

	// -- Inherited from Rml::RenderInterface --

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture_handle) override;

	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei region) override;

	void SetTransform(const Rml::Matrix4f* transform) override;

private:
	enum class shader_type_t : int {
		Vertex,
		Fragment,
		Unknown = -1
	};
	enum class shader_id_t : int {
		Vertex,
		Fragment_WithoutTextures,
		Fragment_WithTextures
	};

	struct shader_vertex_user_data_t {
		// Member objects are order-sensitive to match shader.
		Rml::Matrix4f m_transform;
		Rml::Vector2f m_translate;
	};

	struct texture_data_t {
		VkImage m_p_vk_image;
		VkImageView m_p_vk_image_view;
		VkSampler m_p_vk_sampler;
		VkDescriptorSet m_p_vk_descriptor_set;
		VmaAllocation m_p_vma_allocation;
	};

	struct geometry_handle_t {
		int m_num_indices;

		VkDescriptorBufferInfo m_p_vertex;
		VkDescriptorBufferInfo m_p_index;
		VkDescriptorBufferInfo m_p_shader;

		// @ this is for freeing our logical blocks for VMA
		// see https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/virtual_allocator.html
		VmaVirtualAllocation m_p_vertex_allocation;
		VmaVirtualAllocation m_p_index_allocation;
		VmaVirtualAllocation m_p_shader_allocation;
	};

	struct buffer_data_t {
		VkBuffer m_p_vk_buffer;
		VmaAllocation m_p_vma_allocation;
	};

	class UploadResourceManager {
	public:
		UploadResourceManager()
		    : m_p_device {},
		      m_p_fence {},
		      m_p_command_buffer {},
		      m_p_command_pool {},
		      m_p_graphics_queue {},
		      m_p_submit_mutex {} { }

		~UploadResourceManager() { }

		void Initialize(VkDevice p_device, VkQueue p_queue, uint32_t queue_family_index, std::mutex* p_submit_mutex) {
			RMLUI_VK_ASSERTMSG(p_queue, "you have to pass a valid VkQueue");
			RMLUI_VK_ASSERTMSG(p_device, "you have to pass a valid VkDevice for creation resources");

			m_p_device = p_device;
			m_p_graphics_queue = p_queue;
			m_p_submit_mutex = p_submit_mutex;

			Create_All(queue_family_index);
		}

		void Shutdown() {
			vkDestroyFence(m_p_device, m_p_fence, nullptr);
			vkDestroyCommandPool(m_p_device, m_p_command_pool, nullptr);
		}

		template<typename Func>
		void UploadToGPU(Func&& p_user_commands) noexcept {
			RMLUI_VK_ASSERTMSG(m_p_command_buffer, "you didn't initialize VkCommandBuffer");

			VkCommandBufferBeginInfo info_command = {};

			info_command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			info_command.pNext = nullptr;
			info_command.pInheritanceInfo = nullptr;
			info_command.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			VkResult status = vkBeginCommandBuffer(m_p_command_buffer, &info_command);

			RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkBeginCommandBuffer");

			p_user_commands(m_p_command_buffer);

			status = vkEndCommandBuffer(m_p_command_buffer);

			RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "faield to vkEndCommandBuffer");

			Submit();
			Wait();
		}

	private:
		void Create_Fence() noexcept {
			VkFenceCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			info.pNext = nullptr;
			info.flags = 0;

			VkResult status = vkCreateFence(m_p_device, &info, nullptr, &m_p_fence);

			RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkCreateFence");
		}

		void Create_CommandBuffer() noexcept {
			RMLUI_VK_ASSERTMSG(m_p_command_pool, "you have to initialize VkCommandPool before calling this method!");

			VkCommandBufferAllocateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			info.pNext = nullptr;
			info.commandPool = m_p_command_pool;
			info.commandBufferCount = 1;
			info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

			VkResult status = vkAllocateCommandBuffers(m_p_device, &info, &m_p_command_buffer);

			RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkAllocateCommandBuffers");
		}

		void Create_CommandPool(uint32_t queue_family_index) noexcept {
			VkCommandPoolCreateInfo info = {};

			info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			info.pNext = nullptr;
			info.queueFamilyIndex = queue_family_index;
			info.flags = 0;

			VkResult status = vkCreateCommandPool(m_p_device, &info, nullptr, &m_p_command_pool);

			RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkCreateCommandPool");
		}

		void Create_All(uint32_t queue_family_index) noexcept {
			Create_Fence();
			Create_CommandPool(queue_family_index);
			Create_CommandBuffer();
		}

		void Wait() noexcept {
			RMLUI_VK_ASSERTMSG(m_p_fence, "you must initialize your VkFence");

			vkWaitForFences(m_p_device, 1, &m_p_fence, VK_TRUE, UINT64_MAX);
			vkResetFences(m_p_device, 1, &m_p_fence);
			vkResetCommandPool(m_p_device, m_p_command_pool, 0);
		}

		void Submit() noexcept {
			VkSubmitInfo info = {};

			info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			info.pNext = nullptr;
			info.waitSemaphoreCount = 0;
			info.signalSemaphoreCount = 0;
			info.pSignalSemaphores = nullptr;
			info.pWaitSemaphores = nullptr;
			info.pWaitDstStageMask = nullptr;
			info.pCommandBuffers = &m_p_command_buffer;
			info.commandBufferCount = 1;

			// The render thread submits on the same queue; queues need external synchronization
			std::unique_lock<std::mutex> lock;
			if (m_p_submit_mutex) {
				lock = std::unique_lock<std::mutex>(*m_p_submit_mutex);
			}

			auto status = vkQueueSubmit(m_p_graphics_queue, 1, &info, m_p_fence);

			RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkQueueSubmit");
		}

	private:
		VkDevice m_p_device;
		VkFence m_p_fence;
		VkCommandBuffer m_p_command_buffer;
		VkCommandPool m_p_command_pool;
		VkQueue m_p_graphics_queue;
		std::mutex* m_p_submit_mutex;
	};

	// @ main manager for "allocating" vertex, index, uniform stuff
	class MemoryPool {
	public:
		MemoryPool();
		~MemoryPool();

		void Initialize(
		    VkDeviceSize byte_size, VkDeviceSize device_min_uniform_alignment, VmaAllocator p_allocator, VkDevice p_device
		) noexcept;
		void Shutdown() noexcept;

		bool Alloc_GeneralBuffer(
		    VkDeviceSize size, void** p_data, VkDescriptorBufferInfo* p_out, VmaVirtualAllocation* p_alloc
		) noexcept;
		bool Alloc_VertexBuffer(
		    uint32_t number_of_elements, uint32_t stride_in_bytes, void** p_data, VkDescriptorBufferInfo* p_out,
		    VmaVirtualAllocation* p_alloc
		) noexcept;
		bool Alloc_IndexBuffer(
		    uint32_t number_of_elements, uint32_t stride_in_bytes, void** p_data, VkDescriptorBufferInfo* p_out,
		    VmaVirtualAllocation* p_alloc
		) noexcept;

		void
		    SetDescriptorSet(uint32_t binding_index, uint32_t size, VkDescriptorType descriptor_type, VkDescriptorSet p_set) noexcept;
		void SetDescriptorSet(
		    uint32_t binding_index, VkDescriptorBufferInfo* p_info, VkDescriptorType descriptor_type, VkDescriptorSet p_set
		) noexcept;
		void SetDescriptorSet(
		    uint32_t binding_index, VkSampler p_sampler, VkImageLayout layout, VkImageView p_view, VkDescriptorType descriptor_type,
		    VkDescriptorSet p_set
		) noexcept;

		void Free_GeometryHandle(geometry_handle_t* p_valid_geometry_handle) noexcept;
		void Free_GeometryHandle_ShaderDataOnly(geometry_handle_t* p_valid_geometry_handle) noexcept;

	private:
		VkDeviceSize m_memory_total_size;
		VkDeviceSize m_device_min_uniform_alignment;
		char* m_p_data;
		VkBuffer m_p_buffer;
		VmaAllocation m_p_buffer_alloc;
		VkDevice m_p_device;
		VmaAllocator m_p_vk_allocator;
		VmaVirtualBlock m_p_block;
	};

	// Hands out secondary command buffers for main-thread recording, one pool per buffered
	// frame; pools reset in OnFrameBegin once the render thread is done with that slot
	class CommandBufferRing {
	public:
		static constexpr uint32_t kNumFramesToBuffer = kFramesInFlight;

		CommandBufferRing();

		void Initialize(VkDevice p_device, uint32_t queue_index_graphics) noexcept;
		void Shutdown();

		void OnBeginFrame(uint32_t frame_index);
		VkCommandBuffer GetNextSecondaryBuffer();

	private:
		struct CommandBuffersPerFrame {
			VkCommandPool m_command_pool;
			Rml::Vector<VkCommandBuffer> m_command_buffers;
			uint32_t m_used;
		};

		VkDevice m_p_device;
		uint32_t m_frame_index;
		CommandBuffersPerFrame* m_p_current_frame;
		Rml::Array<CommandBuffersPerFrame, kNumFramesToBuffer> m_frames;
	};

	class DescriptorPoolManager {
	public:
		DescriptorPoolManager() : m_allocated_descriptor_count {}, m_p_descriptor_pool {} { }

		~DescriptorPoolManager() {
			RMLUI_VK_ASSERTMSG(m_allocated_descriptor_count <= 0, "something is wrong. You didn't free some VkDescriptorSet");
		}

		void Initialize(
		    VkDevice p_device, uint32_t count_uniform_buffer, uint32_t count_image_sampler, uint32_t count_sampler,
		    uint32_t count_storage_buffer
		) noexcept {
			RMLUI_VK_ASSERTMSG(p_device, "you can't pass an invalid VkDevice here");

			Rml::Array<VkDescriptorPoolSize, 5> sizes;
			sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, count_uniform_buffer};
			sizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count_uniform_buffer};
			sizes[2] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count_image_sampler};
			sizes[3] = {VK_DESCRIPTOR_TYPE_SAMPLER, count_sampler};
			sizes[4] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count_storage_buffer};

			VkDescriptorPoolCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.pNext = nullptr;
			info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			info.maxSets = 1000;
			info.poolSizeCount = static_cast<uint32_t>(sizes.size());
			info.pPoolSizes = sizes.data();

			auto status = vkCreateDescriptorPool(p_device, &info, nullptr, &m_p_descriptor_pool);
			RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkCreateDescriptorPool");
		}

		void Shutdown(VkDevice p_device) {
			RMLUI_VK_ASSERTMSG(p_device, "you can't pass an invalid VkDevice here");

			vkDestroyDescriptorPool(p_device, m_p_descriptor_pool, nullptr);
		}

		uint32_t Get_AllocatedDescriptorCount() const noexcept { return m_allocated_descriptor_count; }

		bool Alloc_Descriptor(
		    VkDevice p_device, VkDescriptorSetLayout* p_layouts, VkDescriptorSet* p_sets, uint32_t descriptor_count_for_creation = 1
		) noexcept {
			RMLUI_VK_ASSERTMSG(
			    p_layouts, "you have to pass a valid and initialized VkDescriptorSetLayout (probably you must create it)"
			);
			RMLUI_VK_ASSERTMSG(p_device, "you must pass a valid VkDevice here");

			VkDescriptorSetAllocateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			info.pNext = nullptr;
			info.descriptorPool = m_p_descriptor_pool;
			info.descriptorSetCount = descriptor_count_for_creation;
			info.pSetLayouts = p_layouts;

			auto status = vkAllocateDescriptorSets(p_device, &info, p_sets);
			RMLUI_VK_ASSERTMSG(status == VkResult::VK_SUCCESS, "failed to vkAllocateDescriptorSets");

			m_allocated_descriptor_count += descriptor_count_for_creation;

			return status == VkResult::VK_SUCCESS;
		}

		void Free_Descriptors(VkDevice p_device, VkDescriptorSet* p_sets, uint32_t descriptor_count = 1) noexcept {
			RMLUI_VK_ASSERTMSG(p_device, "you must pass a valid VkDevice here");

			if (p_sets) {
				m_allocated_descriptor_count -= descriptor_count;
				vkFreeDescriptorSets(p_device, m_p_descriptor_pool, descriptor_count, p_sets);
			}
		}

	private:
		int m_allocated_descriptor_count;
		VkDescriptorPool m_p_descriptor_pool;
	};

private:
	Rml::TextureHandle CreateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions, const Rml::String& name);

	void Initialize_Resources_Toast(VkQueue queue) noexcept;
	void Destroy_Resources() noexcept;

	void CreateShaders() noexcept;
	void CreateDescriptorSetLayout() noexcept;
	void CreatePipelineLayout() noexcept;
	void CreateDescriptorSets() noexcept;
	void CreateSamplers() noexcept;
	void Create_Pipelines() noexcept;

	buffer_data_t CreateResource_StagingBuffer(VkDeviceSize size, VkBufferUsageFlags flags) noexcept;
	void DestroyResource_StagingBuffer(const buffer_data_t& data) noexcept;

	void Destroy_Textures() noexcept;
	void Destroy_Geometries() noexcept;

	void Destroy_Texture(const texture_data_t& p_texture) noexcept;

	void Destroy_Pipelines() noexcept;
	void DestroyDescriptorSets() noexcept;
	void DestroyPipelineLayout() noexcept;
	void DestroySamplers() noexcept;

	void Update_PendingForDeletion_Textures_By_Frames() noexcept;
	void Update_PendingForDeletion_Geometries() noexcept;

private:
	bool m_is_transform_enabled;
	bool m_is_apply_to_regular_geometry_stencil;
	bool m_is_use_scissor_specified;
	bool m_is_use_stencil_pipeline;

	int m_width;
	int m_height;

	uint32_t m_queue_index_graphics;
	uint32_t m_frame_index;

	VkInstance m_p_instance;
	VkDevice m_p_device;
	VkPhysicalDevice m_p_physical_device;
	VmaAllocator m_p_allocator;
	// @ the secondary command buffer between BeginRecording/EndRecording
	VkCommandBuffer m_p_current_command_buffer;

	VkFormat m_color_attachment_format;
	VkFormat m_depth_stencil_attachment_format;
	std::mutex* m_p_submit_mutex;

	VkDescriptorSetLayout m_p_descriptor_set_layout_vertex_transform;
	VkDescriptorSetLayout m_p_descriptor_set_layout_texture;
	VkPipelineLayout m_p_pipeline_layout;
	VkPipeline m_p_pipeline_with_textures;
	VkPipeline m_p_pipeline_without_textures;
	VkPipeline m_p_pipeline_stencil_for_region_where_geometry_will_be_drawn;
	VkPipeline m_p_pipeline_stencil_for_regular_geometry_that_applied_to_region_with_textures;
	VkPipeline m_p_pipeline_stencil_for_regular_geometry_that_applied_to_region_without_textures;
	VkDescriptorSet m_p_descriptor_set;
	VkSampler m_p_sampler_linear;
	VkRect2D m_scissor;

	// @ means it captures the window size full width and full height, offset equals both x and y to 0
	VkRect2D m_scissor_original;
	VkViewport m_viewport;

	shader_vertex_user_data_t m_user_data_for_vertex_shader;

	Rml::Matrix4f m_projection;
	Rml::Vector<VkShaderModule> m_shaders;
	Rml::Array<Rml::Vector<texture_data_t*>, kFramesInFlight> m_pending_for_deletion_textures_by_frames;

	// vma handles that thing, so there's no need for frame splitting
	Rml::Vector<geometry_handle_t*> m_pending_for_deletion_geometries;

	CommandBufferRing m_command_buffer_ring;
	MemoryPool m_memory_pool;
	UploadResourceManager m_upload_manager;
	DescriptorPoolManager m_manager_descriptors;
};
