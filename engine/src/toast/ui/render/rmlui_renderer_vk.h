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
#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>
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
	static constexpr VkDeviceSize kVideoMemoryForAllocation = 64 * 1024 * 1024;    // [bytes]

	/// Mirrors the EffectsPush block in ui_effects.slang
	struct effects_push_t {
		float m_v[6][4] = {};
	};

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
	std::shared_ptr<const void> OnFrameBegin(uint32_t frame_index);

	/// Begins recording a secondary command buffer for one context render target
	VkCommandBuffer BeginRecording(VkExtent2D extent);
	/// Ends recording and returns the secondary command buffer for execution on the render thread
	VkCommandBuffer EndRecording();

	/// View of the image the last recording rendered into
	VkImageView GetLastOutputView() const { return m_p_last_output_view; }

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

	// RmlUi 6 effects API

	void EnableClipMask(bool enable) override;
	void RenderToClipMask(
	    Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation
	) override;

	Rml::LayerHandle PushLayer() override;
	void CompositeLayers(
	    Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
	    Rml::Span<const Rml::CompiledFilterHandle> filters
	) override;
	void PopLayer() override;

	Rml::TextureHandle SaveLayerAsTexture() override;
	Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;

	Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

	Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void RenderShader(
	    Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
	    Rml::TextureHandle texture
	) override;
	void ReleaseShader(Rml::CompiledShaderHandle shader) override;

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

		// @ this is for freeing our logical blocks for VMA
		// see https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/virtual_allocator.html
		VmaVirtualAllocation m_p_vertex_allocation;
		VmaVirtualAllocation m_p_index_allocation;
	};

	struct buffer_data_t {
		VkBuffer m_p_vk_buffer;
		VmaAllocation m_p_vma_allocation;
	};

	/// Render target used by the layer stack and postprocess passes
	struct effect_image_t {
		VkImage m_p_image = nullptr;
		VkImageView m_p_view = nullptr;
		VmaAllocation m_p_allocation = nullptr;
		VkDescriptorSet m_p_descriptor_set = nullptr;
		VkImageLayout m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	[[nodiscard]]
	static bool IsValidEffectImage(const effect_image_t& image);

	/// All render targets for one extent
	struct LayerPool {
		uint64_t m_generation = 0;
		uint64_t m_last_used_frame = 0;
		VkExtent2D m_extent {};
		std::deque<effect_image_t> m_layers;
		effect_image_t m_postprocess[3];
		effect_image_t m_blend_mask;
		Rml::Vector<effect_image_t> m_outputs;
		effect_image_t m_stencil;
		uint32_t m_outputs_used = 0;
	};

	enum class FilterType {
		Invalid = 0,
		Passthrough,
		Blur,
		DropShadow,
		ColorMatrix,
		MaskImage
	};

	struct CompiledFilter {
		FilterType m_type = FilterType::Invalid;
		float m_blend_factor = 1.f;           // Passthrough
		float m_sigma = 0.f;                  // Blur / DropShadow
		Rml::Vector2f m_offset;               // DropShadow
		Rml::ColourbPremultiplied m_color;    // DropShadow
		Rml::Matrix4f m_color_matrix;         // ColorMatrix
	};

	struct CompiledShader {
		// gradient parameters live in the memory pool for the shader's whole lifetime
		VkDescriptorBufferInfo m_buffer = {};
		VmaVirtualAllocation m_allocation = nullptr;
	};

	/// std140 mirror of GradientData in ui_effects.slang
	struct gradient_data_std140_t {
		int32_t m_func = 0;
		int32_t m_num_stops = 0;
		float m_p[2] = {};
		float m_v[2] = {};
		float m_padding[2] = {};
		float m_stop_colors[16][4] = {};
		float m_stop_positions[16] = {};
	};

	static_assert(sizeof(gradient_data_std140_t) == 352, "must match the std140 layout in ui_effects.slang");

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
		void Flush(const VkDescriptorBufferInfo& range) noexcept;

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
		void Free_Allocation(VmaVirtualAllocation allocation) noexcept;

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
		CommandBufferRing();

		void Initialize(VkDevice p_device, uint32_t queue_index_graphics) noexcept;
		void Shutdown(MemoryPool& memory_pool);

		/// Picks a free slot, resets its pool, and returns its guard
		std::shared_ptr<const void> AcquireSlot(MemoryPool& memory_pool);
		VkCommandBuffer GetNextSecondaryBuffer();

		void AddCurrentSlotAllocation(VmaVirtualAllocation allocation);
		void AddCurrentSlotGeneration(uint64_t generation);
		void AddCurrentSlotResource(std::shared_ptr<const void> resource);
		bool IsGenerationReferenced(uint64_t generation) const;

	private:
		struct Slot {
			VkCommandPool m_command_pool = nullptr;
			Rml::Vector<VkCommandBuffer> m_command_buffers;
			uint32_t m_used = 0;
			std::shared_ptr<const int> m_guard;
			Rml::Vector<VmaVirtualAllocation> m_transient_allocations;
			Rml::Vector<uint64_t> m_generations;
			Rml::Vector<std::shared_ptr<const void>> m_resources;
		};

		Slot& CreateSlot();

		VkDevice m_p_device;
		uint32_t m_queue_index;
		Slot* m_p_current_slot;
		Rml::Vector<Rml::UniquePtr<Slot>> m_slots;
	};

	class DescriptorPoolManager {
	public:
		DescriptorPoolManager() = default;

		~DescriptorPoolManager() {
			RMLUI_VK_ASSERTMSG(m_allocations.empty(), "something is wrong. You didn't free some VkDescriptorSet");
		}

		void Initialize(
		    VkDevice p_device, uint32_t count_uniform_buffer, uint32_t count_image_sampler, uint32_t count_sampler,
		    uint32_t count_storage_buffer
		) noexcept {
			RMLUI_VK_ASSERTMSG(p_device, "you can't pass an invalid VkDevice here");
			m_p_device = p_device;
			m_pool_sizes = {
			  VkDescriptorPoolSize {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, count_uniform_buffer},
			  VkDescriptorPoolSize {        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count_uniform_buffer},
			  VkDescriptorPoolSize {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  count_image_sampler},
			  VkDescriptorPoolSize {               VK_DESCRIPTOR_TYPE_SAMPLER,        count_sampler},
			  VkDescriptorPoolSize {        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count_storage_buffer},
			};
			m_max_sets_per_pool =
			    std::max<uint32_t>(count_uniform_buffer + count_image_sampler + count_sampler + count_storage_buffer, 64);
			RMLUI_VK_ASSERTMSG(CreatePool(), "failed to create the initial descriptor pool");
		}

		void Shutdown(VkDevice p_device) {
			RMLUI_VK_ASSERTMSG(p_device, "you can't pass an invalid VkDevice here");
			for (VkDescriptorPool pool : m_pools) {
				vkDestroyDescriptorPool(p_device, pool, nullptr);
			}
			m_allocations.clear();
			m_pools.clear();
			m_p_device = nullptr;
		}

		uint32_t Get_AllocatedDescriptorCount() const noexcept { return static_cast<uint32_t>(m_allocations.size()); }

		bool Alloc_Descriptor(
		    VkDevice p_device, VkDescriptorSetLayout* p_layouts, VkDescriptorSet* p_sets, uint32_t descriptor_count_for_creation = 1
		) noexcept {
			RMLUI_VK_ASSERTMSG(
			    p_layouts, "you have to pass a valid and initialized VkDescriptorSetLayout (probably you must create it)"
			);
			RMLUI_VK_ASSERTMSG(p_device, "you must pass a valid VkDevice here");
			if (!p_layouts || !p_sets || descriptor_count_for_creation == 0) {
				return false;
			}

			VkDescriptorSetAllocateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			info.pNext = nullptr;
			info.descriptorSetCount = descriptor_count_for_creation;
			info.pSetLayouts = p_layouts;

			auto try_allocate = [&](VkDescriptorPool pool) {
				info.descriptorPool = pool;
				const VkResult status = vkAllocateDescriptorSets(p_device, &info, p_sets);
				if (status == VK_SUCCESS) {
					for (uint32_t i = 0; i < descriptor_count_for_creation; i++) {
						m_allocations.push_back({p_sets[i], pool});
					}
				}
				return status;
			};

			for (VkDescriptorPool pool : m_pools) {
				const VkResult status = try_allocate(pool);
				if (status == VK_SUCCESS) {
					return true;
				}
				if (status != VK_ERROR_OUT_OF_POOL_MEMORY && status != VK_ERROR_FRAGMENTED_POOL) {
					RMLUI_VK_ASSERTMSG(false, "failed to vkAllocateDescriptorSets");
					return false;
				}
			}

			if (!CreatePool()) {
				return false;
			}
			return try_allocate(m_pools.back()) == VK_SUCCESS;
		}

		void Free_Descriptors(VkDevice p_device, VkDescriptorSet* p_sets, uint32_t descriptor_count = 1) noexcept {
			RMLUI_VK_ASSERTMSG(p_device, "you must pass a valid VkDevice here");

			if (!p_sets) {
				return;
			}

			for (uint32_t i = 0; i < descriptor_count; i++) {
				const VkDescriptorSet set = p_sets[i];
				if (!set) {
					continue;
				}

				auto allocation = std::find_if(m_allocations.begin(), m_allocations.end(), [set](const Allocation& candidate) {
					return candidate.m_set == set;
				});
				if (allocation == m_allocations.end()) {
					RMLUI_VK_ASSERTMSG(false, "descriptor set was not allocated by this manager");
					continue;
				}

				vkFreeDescriptorSets(p_device, allocation->m_pool, 1, &set);
				m_allocations.erase(allocation);
				p_sets[i] = nullptr;
			}
		}

	private:
		struct Allocation {
			VkDescriptorSet m_set = nullptr;
			VkDescriptorPool m_pool = nullptr;
		};

		bool CreatePool() noexcept {
			VkDescriptorPoolCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			info.maxSets = m_max_sets_per_pool;
			info.poolSizeCount = static_cast<uint32_t>(m_pool_sizes.size());
			info.pPoolSizes = m_pool_sizes.data();

			VkDescriptorPool pool = nullptr;
			const VkResult status = vkCreateDescriptorPool(m_p_device, &info, nullptr, &pool);
			if (status != VK_SUCCESS) {
				RMLUI_VK_ASSERTMSG(false, "failed to vkCreateDescriptorPool");
				return false;
			}
			m_pools.push_back(pool);
			return true;
		}

		VkDevice m_p_device = nullptr;
		uint32_t m_max_sets_per_pool = 0;
		std::array<VkDescriptorPoolSize, 5> m_pool_sizes {};
		Rml::Vector<VkDescriptorPool> m_pools;
		Rml::Vector<Allocation> m_allocations;
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

	void Destroy_Texture(const texture_data_t& p_texture) noexcept;
	bool RetainResource(const void* resource);
	void ReleaseResource(const void* resource);

	void Destroy_Pipelines() noexcept;
	void DestroyDescriptorSets() noexcept;
	void DestroyPipelineLayout() noexcept;
	void DestroySamplers() noexcept;

	void CreateEffectResources() noexcept;
	void DestroyEffectResources() noexcept;

	LayerPool& AcquirePool(VkExtent2D extent);
	void DestroyPool(LayerPool& pool) noexcept;
	void RetireUnusedPools() noexcept;
	effect_image_t CreateEffectImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage) noexcept;
	void DestroyEffectImage(effect_image_t& image) noexcept;

	void TransitionEffectImage(effect_image_t& image, VkImageLayout new_layout);
	void BeginLayerScope(effect_image_t& target, bool clear_color, bool clear_stencil);
	void BeginEffectScope(effect_image_t& target, bool clear_color, VkRect2D render_area, bool use_clip_stencil = false);
	void EndScope();

	void SuspendLayerScope();
	void ResumeLayerScope();

	void RenderFullscreenPass(
	    VkPipeline pipeline, effect_image_t& destination, effect_image_t& source, effect_image_t* mask, const effects_push_t& push,
	    bool clear_destination, VkRect2D render_area, VkViewport viewport, bool use_clip_stencil = false
	);

	void RenderFilters(Rml::Span<const Rml::CompiledFilterHandle> filter_handles);
	void RenderBlur(float sigma, LayerPool& pool, int source_destination, int temp);

	VkRect2D CurrentScissor() const;
	VkDescriptorSet GetEffectDescriptorSet(effect_image_t& image) noexcept;

private:
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
	std::unordered_map<const void*, std::shared_ptr<const void>> m_live_resources;

	CommandBufferRing m_command_buffer_ring;
	MemoryPool m_memory_pool;
	UploadResourceManager m_upload_manager;
	DescriptorPoolManager m_manager_descriptors;

	uint64_t m_pool_generation = 0;
	uint64_t m_pool_frame = 0;
	Rml::Vector<Rml::UniquePtr<LayerPool>> m_pools;
	Rml::Vector<Rml::UniquePtr<LayerPool>> m_retired_pools;
	LayerPool* m_p_current_pool = nullptr;

	// recording state
	Rml::Vector<effect_image_t*> m_layer_stack;
	uint32_t m_stack_layers_used = 0;
	bool m_scope_active = false;
	effect_image_t* m_p_scope_target = nullptr;
	int m_stencil_test_value = 1;
	VkImageView m_p_last_output_view = nullptr;

	VkSampler m_p_sampler_clamp = nullptr;

	// fullscreen effect pipelines
	// layout = [texture set, mask set] + 96B push constants
	VkDescriptorSetLayout m_p_layout_effect_texture = nullptr;
	VkPipelineLayout m_p_pipeline_layout_effects = nullptr;
	VkPipeline m_p_pipeline_passthrough_blend = nullptr;
	VkPipeline m_p_pipeline_passthrough_noblend = nullptr;
	VkPipeline m_p_pipeline_passthrough_blend_stencil = nullptr;
	VkPipeline m_p_pipeline_passthrough_noblend_stencil = nullptr;
	VkPipeline m_p_pipeline_colormatrix = nullptr;
	VkPipeline m_p_pipeline_blendmask = nullptr;
	VkPipeline m_p_pipeline_blur = nullptr;
	VkPipeline m_p_pipeline_dropshadow = nullptr;

	// gradient pipeline
	// layout = [vertex transform set, gradient data set]
	VkDescriptorSetLayout m_p_layout_gradient_data = nullptr;
	VkPipelineLayout m_p_pipeline_layout_gradient = nullptr;
	VkPipeline m_p_pipeline_gradient = nullptr;
	VkPipeline m_p_pipeline_gradient_stencil = nullptr;
	VkDescriptorSet m_p_descriptor_set_gradient = nullptr;

	// clip mask writes reuse the stencil pipeline
	// Intersect needs an increment variant
	VkPipeline m_p_pipeline_clip_write_incr = nullptr;
	bool m_clip_incr = false;
	uint32_t m_clip_write_value = 1;

	VkShaderModule m_p_effects_shader_module = nullptr;
};
