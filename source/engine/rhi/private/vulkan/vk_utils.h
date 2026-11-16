#pragma once

#include <volk.h>

#include <cstdint>

namespace buz
{
	namespace vk
	{
		//----------------------------------------------------------
		/// @brief Checks the result of a Vulkan call and logs an error when it failed.
		/// @param result Result returned by the Vulkan call.
		/// @param what Human readable description of the call, used in the error message.
		/// @return True when the call succeeded.
		bool verify(VkResult result, const char* what);

		//----------------------------------------------------------
		/// @brief Builds a barrier that moves an image from one layout to
		/// another: Vulkan's counterpart of a DirectX 12 transition barrier.
		/// Besides the layouts it names what must finish before (source
		/// stage and access) and what waits for it (destination).
		VkImageMemoryBarrier2 make_image_barrier(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access, VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access);

		//----------------------------------------------------------
		/// @brief Records one image barrier.
		void record_barrier(VkCommandBuffer command_buffer, const VkImageMemoryBarrier2& barrier);
	}
}
