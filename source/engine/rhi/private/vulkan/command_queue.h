#pragma once

#include <volk.h>

#include <cstdint>

namespace buz::vulkan
{
	//--------------------------------------------------------------
	/// @brief Wraps the Vulkan queue together with a timeline semaphore.
	///
	/// The gpu executes command buffers asynchronously: submitting work only
	/// queues it. A timeline semaphore is a counter the gpu increments when
	/// it reaches a certain point in the queue, which lets the cpu wait
	/// until submitted work has actually finished. It is exactly what a
	/// fence is in DirectX 12, so this class offers the same functions as
	/// d3d12::command_queue.
	class command_queue
	{
	public:
		//----------------------------------------------------------
		/// @brief Creates an empty command_queue; initialize sets it up.
		command_queue();
		~command_queue();

		command_queue(const command_queue&) = delete;
		command_queue& operator=(const command_queue&) = delete;

		//----------------------------------------------------------
		/// @brief Fetches the queue and creates its timeline semaphore.
		/// @param vk_device Device the queue belongs to.
		/// @param queue_family Family the queue was created in.
		/// @return True when the queue is ready for use.
		bool initialize(VkDevice vk_device, uint32_t queue_family);

		//----------------------------------------------------------
		/// @brief Returns the underlying Vulkan queue.
		VkQueue get_vk_queue() const;

		//----------------------------------------------------------
		/// @brief Submits a command buffer for execution on the gpu.
		/// @param command_buffer Recorded command buffer to execute.
		/// @param wait_semaphore Binary semaphore the gpu waits for before
		/// writing to the swap chain image (VK_NULL_HANDLE for none).
		/// @param signal_semaphore Binary semaphore signalled when done, for
		/// present to wait on (VK_NULL_HANDLE for none). DirectX needs
		/// neither: its swap chain synchronizes with the queue by itself.
		/// @return Fence value that is reached when the gpu finished it.
		uint64_t submit(VkCommandBuffer command_buffer, VkSemaphore wait_semaphore = VK_NULL_HANDLE, VkSemaphore signal_semaphore = VK_NULL_HANDLE);

		//----------------------------------------------------------
		/// @brief Inserts a fence signal in the queue and returns its value.
		/// @return Fence value that is reached when all previously submitted work finished.
		uint64_t signal();

		//----------------------------------------------------------
		/// @brief Returns the fence value the next submit or signal will use:
		/// work recorded now is done once the gpu reaches it.
		uint64_t get_next_fence_value() const;

		//----------------------------------------------------------
		/// @brief Returns the highest fence value the gpu has reached.
		uint64_t get_completed_value() const;

		//----------------------------------------------------------
		/// @brief Blocks the cpu until the gpu reached the given fence value.
		/// @param fence_value Fence value to wait for, as returned by submit() or signal().
		void wait_for_fence_value(uint64_t fence_value);

		//----------------------------------------------------------
		/// @brief Blocks the cpu until the gpu finished all submitted work.
		void flush();

	private:
		VkDevice m_device;
		VkQueue m_queue;
		VkSemaphore m_timeline;

		uint64_t m_next_fence_value;
	};
}
