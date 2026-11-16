#include "vulkan/command_queue.h"

#include "vulkan/vk_utils.h"

namespace buz::vulkan
{
	//--------------------------------------------------------------
	command_queue::command_queue()
		: m_device(VK_NULL_HANDLE)
		, m_queue(VK_NULL_HANDLE)
		, m_timeline(VK_NULL_HANDLE)
		, m_next_fence_value(1)
	{
	}

	//--------------------------------------------------------------
	command_queue::~command_queue()
	{
		if (m_timeline != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(m_device, m_timeline, nullptr);
		}
	}

	//--------------------------------------------------------------
	bool command_queue::initialize(VkDevice vk_device, uint32_t queue_family)
	{
		m_device = vk_device;
		vkGetDeviceQueue(vk_device, queue_family, 0, &m_queue);

		const VkSemaphoreTypeCreateInfo timeline_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
			.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
			.initialValue = 0,
		};

		const VkSemaphoreCreateInfo semaphore_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = &timeline_info,
		};

		return vk::verify(vkCreateSemaphore(vk_device, &semaphore_info, nullptr, &m_timeline), "vkCreateSemaphore (timeline)");
	}

	//--------------------------------------------------------------
	uint64_t command_queue::submit(VkCommandBuffer command_buffer, VkSemaphore wait_semaphore, VkSemaphore signal_semaphore)
	{
		const uint64_t fence_value = m_next_fence_value++;

		// Writing to the swap chain image has to wait until the image is
		// free; everything before that may already run.
		const VkSemaphoreSubmitInfo wait_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = wait_semaphore,
			.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		};

		// The timeline semaphore reaches fence_value when all work is done;
		// the optional binary one tells present the image is finished.
		const VkSemaphoreSubmitInfo signal_infos[] = {
			{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = m_timeline,
				.value = fence_value,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			},
			{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = signal_semaphore,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			},
		};

		const VkCommandBufferSubmitInfo command_buffer_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = command_buffer,
		};

		const VkSubmitInfo2 submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.waitSemaphoreInfoCount = wait_semaphore != VK_NULL_HANDLE ? 1u : 0u,
			.pWaitSemaphoreInfos = &wait_info,
			.commandBufferInfoCount = command_buffer != VK_NULL_HANDLE ? 1u : 0u,
			.pCommandBufferInfos = &command_buffer_info,
			.signalSemaphoreInfoCount = signal_semaphore != VK_NULL_HANDLE ? 2u : 1u,
			.pSignalSemaphoreInfos = signal_infos,
		};

		vk::verify(vkQueueSubmit2(m_queue, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit2");
		return fence_value;
	}

	//--------------------------------------------------------------
	uint64_t command_queue::signal()
	{
		// A submit without work only moves the timeline forward
		return submit(VK_NULL_HANDLE);
	}

	//--------------------------------------------------------------
	uint64_t command_queue::get_completed_value() const
	{
		uint64_t value = 0;
		vkGetSemaphoreCounterValue(m_device, m_timeline, &value);
		return value;
	}

	//--------------------------------------------------------------
	void command_queue::wait_for_fence_value(uint64_t fence_value)
	{
		if (get_completed_value() >= fence_value)
		{
			return;
		}

		const VkSemaphoreWaitInfo wait_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = 1,
			.pSemaphores = &m_timeline,
			.pValues = &fence_value,
		};
		vkWaitSemaphores(m_device, &wait_info, UINT64_MAX);
	}

	//--------------------------------------------------------------
	void command_queue::flush()
	{
		// Nothing to wait for when initialization never got this far
		if (m_timeline == VK_NULL_HANDLE)
		{
			return;
		}

		wait_for_fence_value(signal());
	}

	//--------------------------------------------------------------
	VkQueue command_queue::get_vk_queue() const
	{
		return m_queue;
	}

	//--------------------------------------------------------------
	uint64_t command_queue::get_next_fence_value() const
	{
		return m_next_fence_value;
	}
}
