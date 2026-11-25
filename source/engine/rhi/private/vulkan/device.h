#pragma once

#include "vulkan/adapter_selection.h"

#include <volk.h>
#include <vk_mem_alloc.h>

struct SDL_Window;

namespace buz::vulkan
{
	//--------------------------------------------------------------
	/// @brief Owns the Vulkan instance, the window surface and the device
	/// created on the selected graphics card.
	///
	/// Initialization loads Vulkan from the driver (volk), enables the
	/// validation layer (in debug builds, when installed), scores all
	/// graphics cards, selects one based on the given preference and creates
	/// the device on it, plus the memory allocator (VMA) that hands out gpu
	/// memory for textures and buffers.
	class device
	{
	public:
		//----------------------------------------------------------
		/// @brief Creates an empty device; initialize sets it up.
		device();
		~device();

		device(const device&) = delete;
		device& operator=(const device&) = delete;

		//----------------------------------------------------------
		/// @brief Creates the instance, surface, device and memory allocator.
		/// @param window Window the device presents to (created with Vulkan support).
		/// @param preference Whether to run on the highest or lowest scoring graphics card.
		/// @return True when the device was created successfully.
		bool initialize(SDL_Window* window, rhi::adapter_preference preference);

		//----------------------------------------------------------
		/// @brief Returns the Vulkan instance.
		VkInstance get_instance() const;

		//----------------------------------------------------------
		/// @brief Returns the window's surface.
		VkSurfaceKHR get_surface() const;

		//----------------------------------------------------------
		/// @brief Returns the graphics card the device runs on.
		VkPhysicalDevice get_physical_device() const;

		//----------------------------------------------------------
		/// @brief Returns the Vulkan device.
		VkDevice get_vk_device() const;

		//----------------------------------------------------------
		/// @brief Returns the memory allocator (VMA).
		VmaAllocator get_allocator() const;

		//----------------------------------------------------------
		/// @brief Returns information about the graphics card the device runs on.
		const adapter_info& get_adapter_info() const;

	private:
		//----------------------------------------------------------
		/// @brief Creates the instance, with the validation layer when available.
		bool create_instance();

		VkInstance m_instance;
		VkDebugUtilsMessengerEXT m_debug_messenger;
		VkSurfaceKHR m_surface;
		VkDevice m_device;
		VmaAllocator m_allocator;

		adapter_info m_adapter_info;
	};
}
