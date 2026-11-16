#include "vulkan/adapter_selection.h"

#include "core/log.h"

#include <cstring>

namespace
{
	//--------------------------------------------------------------
	/// @brief Returns true when the device supports the given extension.
	bool has_extension(VkPhysicalDevice physical_device, const char* extension_name)
	{
		uint32_t count = 0;
		vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);

		std::vector<VkExtensionProperties> extensions(count);
		vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data());

		for (const VkExtensionProperties& extension : extensions)
		{
			if (std::strcmp(extension.extensionName, extension_name) == 0)
			{
				return true;
			}
		}
		return false;
	}

	//--------------------------------------------------------------
	/// @brief Returns true when the device supports every feature buz uses.
	bool has_required_features(VkPhysicalDevice physical_device)
	{
		VkPhysicalDeviceVulkan13Features features_13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceVulkan12Features features_12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &features_13 };
		VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features_12 };
		vkGetPhysicalDeviceFeatures2(physical_device, &features);

		return features_13.dynamicRendering && features_13.synchronization2 && features_12.timelineSemaphore && features.features.fillModeNonSolid;
	}

	//--------------------------------------------------------------
	/// @brief Finds a queue family that can draw and present to the surface.
	/// @return True when one was found.
	bool find_graphics_queue_family(VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t& out_family)
	{
		uint32_t count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);

		std::vector<VkQueueFamilyProperties> families(count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, families.data());

		for (uint32_t family = 0; family < count; ++family)
		{
			VkBool32 can_present = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, family, surface, &can_present);

			if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && can_present)
			{
				out_family = family;
				return true;
			}
		}
		return false;
	}

	//--------------------------------------------------------------
	/// @brief Returns the video memory a card has for itself.
	///
	/// Vulkan lists memory heaps instead of a single number; the "device
	/// local" heaps of a discrete card are its own memory. An integrated
	/// gpu borrows system memory, so like DirectX's DedicatedVideoMemory
	/// it counts as having next to none.
	uint64_t dedicated_video_memory(VkPhysicalDevice physical_device, VkPhysicalDeviceType type)
	{
		if (type != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			return 0;
		}

		VkPhysicalDeviceMemoryProperties memory = {};
		vkGetPhysicalDeviceMemoryProperties(physical_device, &memory);

		uint64_t total = 0;
		for (uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap)
		{
			if ((memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
			{
				total += memory.memoryHeaps[heap].size;
			}
		}
		return total;
	}

	//--------------------------------------------------------------
	/// @brief Calculates the capability score of a card: dedicated video
	/// memory in megabytes, with the Vulkan version as tie breaker.
	uint64_t calculate_score(const buz::vulkan::adapter_info& info)
	{
		if (info.is_software)
		{
			return 0;
		}

		const uint64_t memory_in_mb = info.dedicated_video_memory / (1024 * 1024);
		const uint64_t version_bonus = static_cast<uint64_t>(VK_API_VERSION_MINOR(info.api_version)) * 100;

		return memory_in_mb + version_bonus;
	}
}

namespace buz::vulkan
{
	//--------------------------------------------------------------
	std::vector<adapter_info> enumerate_and_score_adapters(VkInstance instance, VkSurfaceKHR surface)
	{
		uint32_t count = 0;
		vkEnumeratePhysicalDevices(instance, &count, nullptr);

		std::vector<VkPhysicalDevice> physical_devices(count);
		vkEnumeratePhysicalDevices(instance, &count, physical_devices.data());

		std::vector<adapter_info> adapters;
		adapters.reserve(physical_devices.size());
		for (VkPhysicalDevice physical_device : physical_devices)
		{
			VkPhysicalDeviceProperties properties = {};
			vkGetPhysicalDeviceProperties(physical_device, &properties);

			if (properties.apiVersion < VK_API_VERSION_1_3)
			{
				log::warn("Skipping adapter \"{}\": no Vulkan 1.3 support", properties.deviceName);
				continue;
			}

			adapter_info info;
			if (!find_graphics_queue_family(physical_device, surface, info.graphics_queue_family))
			{
				log::warn("Skipping adapter \"{}\": it cannot present to the window", properties.deviceName);
				continue;
			}

			if (!has_extension(physical_device, VK_KHR_SWAPCHAIN_EXTENSION_NAME) || !has_required_features(physical_device))
			{
				log::warn("Skipping adapter \"{}\": missing swap chain, dynamic rendering, synchronization2, timeline semaphore or wireframe support", properties.deviceName);
				continue;
			}

			info.physical_device = physical_device;
			info.description = properties.deviceName;
			info.dedicated_video_memory = dedicated_video_memory(physical_device, properties.deviceType);
			info.is_software = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
			info.api_version = properties.apiVersion;
			info.score = calculate_score(info);

			const uint64_t video_memory_mb = info.dedicated_video_memory / (1024 * 1024);
			const uint32_t major_version = VK_API_VERSION_MAJOR(info.api_version);
			const uint32_t minor_version = VK_API_VERSION_MINOR(info.api_version);
			const char* software_note = info.is_software ? " [software]" : "";
			log::info("Found adapter: {} (video memory: {} MB, Vulkan {}.{}, score: {}){}", info.description, video_memory_mb, major_version, minor_version, info.score, software_note);

			adapters.push_back(info);
		}

		return adapters;
	}
}
