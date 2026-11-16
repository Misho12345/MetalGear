#include "vulkan/vk_utils.h"

#include "core/log.h"

namespace buz::vk
{
	//--------------------------------------------------------------
	bool verify(VkResult result, const char* what)
	{
		if (result != VK_SUCCESS)
		{
			log::error("{} failed (VkResult {})", what, static_cast<int>(result));
			return false;
		}

		return true;
	}

	//--------------------------------------------------------------
	VkImageMemoryBarrier2 make_image_barrier(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access, VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access)
	{
		return VkImageMemoryBarrier2{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = source_stage,
			.srcAccessMask = source_access,
			.dstStageMask = destination_stage,
			.dstAccessMask = destination_access,
			.oldLayout = old_layout,
			.newLayout = new_layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
		};
	}

	//--------------------------------------------------------------
	void record_barrier(VkCommandBuffer command_buffer, const VkImageMemoryBarrier2& barrier)
	{
		const VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &barrier,
		};
		vkCmdPipelineBarrier2(command_buffer, &dependency);
	}
}
