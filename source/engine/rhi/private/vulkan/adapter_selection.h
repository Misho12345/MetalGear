#pragma once

#include "rhi/settings.h"

#include <volk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace buz::vulkan
{
	//--------------------------------------------------------------
	/// @brief Information and capability score of a single graphics card.
	struct adapter_info
	{
		VkPhysicalDevice physical_device = VK_NULL_HANDLE; // the card itself

		std::string description;             // its name, for the log
		uint64_t dedicated_video_memory = 0; // in bytes
		bool is_software = false;            // a software rasterizer (lavapipe)

		uint32_t api_version = 0;           // the highest Vulkan version it supports
		uint32_t graphics_queue_family = 0; // draws and presents to the window

		uint64_t score = 0; // higher is a more capable card
	};

	//--------------------------------------------------------------
	/// @brief Enumerates the graphics cards that can run buz with Vulkan and
	/// scores them.
	///
	/// The same rule as the DirectX 12 backend: the amount of dedicated
	/// video memory dominates the score, the Vulkan version is the tie
	/// breaker, and software rasterizers score zero; rhi::select_adapter
	/// (common/adapter_scoring.h) only picks them when no real graphics
	/// card is present. A card needs Vulkan 1.3 and a queue that can both
	/// draw and present to the window's surface.
	///
	/// @param instance Instance used to enumerate the cards.
	/// @param surface Surface of the window the card must present to.
	/// @return All suitable graphics cards, in enumeration order.
	std::vector<adapter_info> enumerate_and_score_adapters(VkInstance instance, VkSurfaceKHR surface);
}
