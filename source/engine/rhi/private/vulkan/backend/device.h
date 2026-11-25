#pragma once

// The Vulkan implementation of the rhi: the `native` half of every rhi
// class, one file per class in this folder. Its layout mirrors the DirectX 12
// backend (d3d12/backend/) on purpose: the same classes, the same frame loop
// and the same resource lifetime rules, so reading one explains the other and
// a fix in one shows where the other needs it too.

#include "common/constants.h"
#include "common/data_slot.h"
#include "common/frame_scratch.h"
#include "common/lifetime_tracker.h"
#include "rhi/command_list.h"
#include "rhi/device.h"
#include "vulkan/command_queue.h"
#include "vulkan/descriptor_heap.h"
#include "vulkan/device.h"
#include "vulkan/pso_cache.h"
#include "vulkan/swap_chain.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace buz::rhi
{
	//--------------------------------------------------------------
	/// @brief The Vulkan device, queue, swap chain and frame loop.
	class device::native
	{
	public:
		//----------------------------------------------------------
		/// @brief Creates an empty device; initialize sets it up.
		native();

		~native();

		//----------------------------------------------------------
		/// @brief Creates the device, queue, swap chain and descriptor heap.
		/// @return True when everything was created successfully.
		bool initialize(const device_desc& desc);

		//----------------------------------------------------------
		// The rhi::device functions (see rhi/device.h)
		std::unique_ptr<gpu_texture> create_texture(uint32_t width, uint32_t height, std::span<const uint8_t> rgba_pixels);
		std::unique_ptr<buffer> create_buffer(buffer_usage usage, std::span<const std::byte> data, uint32_t stride);
		std::unique_ptr<pipeline> create_pipeline(const pipeline_desc& desc);

		command_list& begin_frame(color clear_color);
		void end_frame();
		void shutdown();

		frame_stats get_frame_stats() const;
		void request_capture();
		std::vector<uint8_t> read_capture();
		uint32_t get_width() const;
		uint32_t get_height() const;

		//----------------------------------------------------------
		/// @brief Frees a destroyed resource's api objects once the gpu is
		/// done with every frame recorded so far. Called by the resources.
		void release_later(std::function<void()> release);

		//----------------------------------------------------------
		/// @brief Counts a newly created resource. Called by the resources.
		void resource_created();

		//----------------------------------------------------------
		/// @brief Returns the heap holding the texture descriptors.
		vulkan::descriptor_heap& get_texture_heap();

		//----------------------------------------------------------
		/// @brief A piece of the current frame's scratch memory.
		struct scratch_allocation
		{
			std::byte* data = nullptr;        // where the cpu writes
			VkBuffer buffer = VK_NULL_HANDLE; // where the gpu reads...
			VkDeviceSize offset = 0;          // ...starting here
		};

		//----------------------------------------------------------
		/// @brief Reserves scratch memory in the frame being recorded.
		/// @param alignment The offset is a multiple of this (see frame_scratch::allocate).
		/// @return The allocation, or nothing when the frame's scratch is full.
		std::optional<scratch_allocation> allocate_scratch(uint32_t size, uint32_t alignment = k_frame_scratch_alignment);

		//----------------------------------------------------------
		/// @brief Returns the descriptor set of a data slot in the frame
		/// being recorded: a dynamic uniform buffer over the frame's scratch
		/// memory, bound at set 2 + slot with the data's offset.
		VkDescriptorSet get_data_set(data_slot slot) const;

		//----------------------------------------------------------
		/// @brief Returns the Vulkan device, for the resources' destructors.
		VkDevice get_vk_device() const;

		//----------------------------------------------------------
		/// @brief Returns the memory allocator, for the resources' destructors.
		VmaAllocator get_allocator() const;

	private:
		//----------------------------------------------------------
		/// @brief Everything one frame in flight needs of its own.
		struct frame
		{
			// A command pool can only be reset once the gpu finished the
			// frame that used it; fence_value says when that is.
			VkCommandPool command_pool = VK_NULL_HANDLE;
			VkCommandBuffer command_buffer = VK_NULL_HANDLE;
			VkSemaphore image_acquired = VK_NULL_HANDLE; // the swap chain signals it
			uint64_t fence_value = 0;

			// Host visible memory for data that only lives for this frame,
			// mapped for the whole lifetime of the backend.
			VkBuffer scratch_buffer = VK_NULL_HANDLE;
			VmaAllocation scratch_allocation = VK_NULL_HANDLE;
			std::byte* scratch_data = nullptr;
			frame_scratch scratch;

			// Indexed by data_slot: a dynamic uniform buffer over
			// scratch_buffer, get_data_size(slot) bytes wide, which
			// command_list::set_data binds at the data's offset.
			VkDescriptorSet data_sets[k_data_slot_count] = {};
		};

		//----------------------------------------------------------
		/// @brief A sampler baked into a descriptor set, one per filter:
		/// what the static sampler in a DirectX 12 root signature is.
		struct sampler_set
		{
			VkSampler sampler = VK_NULL_HANDLE;
			VkDescriptorSetLayout layout = VK_NULL_HANDLE;
			VkDescriptorSet set = VK_NULL_HANDLE;
		};

		//----------------------------------------------------------
		/// @brief Creates the two sampler sets (linear and nearest).
		bool create_sampler_sets();

		//----------------------------------------------------------
		/// @brief Creates the set layout of the shader data and the frame
		/// data (sets 2 and 3), and every frame's descriptor sets of them.
		/// Called once the frames' scratch buffers exist.
		bool create_data_sets();

		//----------------------------------------------------------
		/// @brief Creates a command pool with one command buffer in it.
		bool create_command_buffer(VkCommandPool& out_pool, VkCommandBuffer& out_command_buffer);

		// Qualified: inside rhi::device, a plain "device" names rhi::device
		vulkan::device m_device;
		vulkan::command_queue m_queue;
		vulkan::swap_chain m_swap_chain;
		vulkan::descriptor_heap m_texture_heap;
		vulkan::pso_cache m_pso_cache;
		lifetime_tracker m_lifetime;

		// One depth image, the size of the swap chain images, shared by
		// every frame: the queue runs frames one after another, and the
		// barrier at the start of each frame waits for the previous one.
		VkImage m_depth_image;
		VmaAllocation m_depth_allocation;
		VkImageView m_depth_view;

		VkDescriptorPool m_sampler_pool; // holds the sampler sets
		sampler_set m_sampler_sets[2];   // indexed by sampler_filter

		// The shader data and the frame data (common/data_slot.h): one set
		// layout for both, and every frame's sets in their own pool
		VkDescriptorSetLayout m_data_layout;
		VkDescriptorPool m_data_pool;

		frame m_frames[k_frames_in_flight]; // used round robin, one per frame in flight
		uint32_t m_frame_index;             // which of m_frames is being recorded
		uint32_t m_image_index;             // which swap chain image it draws into
		command_list m_command_list;        // records the frame being drawn
		frame_stats m_last_frame_stats;     // what the previous frame asked of the gpu

		// Frame capture: a host visible buffer the finished swap chain image
		// is copied into.
		bool m_capture_requested;
		VkBuffer m_capture_buffer;
		VmaAllocation m_capture_allocation;
		std::byte* m_capture_data;
		uint64_t m_capture_fence_value;

		// One per swap chain image: signalled when drawing into that image
		// is done, present waits for it.
		std::vector<VkSemaphore> m_rendering_done;

		// Reused for every texture upload
		VkCommandPool m_upload_pool;
		VkCommandBuffer m_upload_command_buffer;

		bool m_is_shut_down; // shutdown ran; the destructor does not run it again
	};
}
