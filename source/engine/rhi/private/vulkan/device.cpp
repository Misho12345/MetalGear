#include "vulkan/device.h"

#include "common/adapter_scoring.h"
#include "core/assert.h"
#include "core/log.h"
#include "vulkan/vk_utils.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

#include <cstring>
#include <vector>

namespace
{
	constexpr const char* k_validation_layer = "VK_LAYER_KHRONOS_validation";

	//--------------------------------------------------------------
	/// @brief Receives the validation layer's messages and logs them; in
	/// debug builds an error also stops in the debugger, just like the
	/// DirectX 12 debug layer does.
	VKAPI_ATTR VkBool32 VKAPI_CALL on_validation_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT /*types*/, const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user_data*/)
	{
		if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
		{
			buz::log::error("Vulkan: {}", data->pMessage);
			BUZ_DEBUG_BREAK();
		}
		else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
		{
			buz::log::warn("Vulkan: {}", data->pMessage);
		}

		return VK_FALSE;
	}

#if !defined(NDEBUG)
	//--------------------------------------------------------------
	/// @brief Returns true when the Khronos validation layer is installed
	/// (it comes with the Vulkan SDK, not with the graphics driver).
	bool is_validation_layer_available()
	{
		uint32_t count = 0;
		vkEnumerateInstanceLayerProperties(&count, nullptr);

		std::vector<VkLayerProperties> layers(count);
		vkEnumerateInstanceLayerProperties(&count, layers.data());

		for (const VkLayerProperties& layer : layers)
		{
			if (std::strcmp(layer.layerName, k_validation_layer) == 0)
			{
				return true;
			}
		}
		return false;
	}
#endif
}

namespace buz::vulkan
{
	//--------------------------------------------------------------
	device::device()
		: m_instance(VK_NULL_HANDLE)
		, m_debug_messenger(VK_NULL_HANDLE)
		, m_surface(VK_NULL_HANDLE)
		, m_device(VK_NULL_HANDLE)
		, m_allocator(VK_NULL_HANDLE)
	{
	}

	//--------------------------------------------------------------
	device::~device()
	{
		if (m_allocator != VK_NULL_HANDLE)
		{
			vmaDestroyAllocator(m_allocator);
		}
		if (m_device != VK_NULL_HANDLE)
		{
			vkDestroyDevice(m_device, nullptr);
		}
		if (m_surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
		}
		if (m_debug_messenger != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(m_instance, m_debug_messenger, nullptr);
		}
		if (m_instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(m_instance, nullptr);
		}
	}

	//--------------------------------------------------------------
	bool device::initialize(SDL_Window* window, rhi::adapter_preference preference)
	{
		// volk finds the Vulkan loader the graphics driver installed and
		// fetches its functions; no Vulkan SDK is needed to run.
		if (!vk::verify(volkInitialize(), "volkInitialize"))
		{
			log::error("Vulkan is not available: is a graphics driver with Vulkan support installed?");
			return false;
		}

		if (!create_instance())
		{
			return false;
		}

		if (!SDL_Vulkan_CreateSurface(window, m_instance, nullptr, &m_surface))
		{
			log::error("Failed to create the window surface: {}", SDL_GetError());
			return false;
		}

		// Score all graphics cards on this machine and pick one
		const std::vector<adapter_info> adapters = enumerate_and_score_adapters(m_instance, m_surface);

		const adapter_info* selected = rhi::select_adapter(adapters, preference);
		if (selected == nullptr)
		{
			log::error("{}", preference == rhi::adapter_preference::software ? "No software rasterizer found" : "No graphics card with Vulkan 1.3 support found");
			return false;
		}

		m_adapter_info = *selected;
		log::info("Selected adapter: {} ({})", m_adapter_info.description, rhi::describe(preference));

		// Create the real device on the selected card, with one queue that
		// draws and presents, and the Vulkan 1.2/1.3 features buz relies on.
		const float queue_priority = 1.0f;
		const VkDeviceQueueCreateInfo queue_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = m_adapter_info.graphics_queue_family,
			.queueCount = 1,
			.pQueuePriorities = &queue_priority,
		};

		VkPhysicalDeviceVulkan13Features features_13 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.shaderDemoteToHelperInvocation = VK_TRUE, // dxc turns hlsl's discard into a demote
			.synchronization2 = VK_TRUE, // simpler barriers and submits
			.dynamicRendering = VK_TRUE, // draw into images without render pass objects
		};

		VkPhysicalDeviceVulkan12Features features_12 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &features_13,
			.timelineSemaphore = VK_TRUE, // a fence with a counter, like DirectX 12's
		};

		const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		// Core features: wireframe drawing (a debug view) needs non-solid fill
		const VkPhysicalDeviceFeatures2 features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &features_12,
			.features = { .fillModeNonSolid = VK_TRUE },
		};

		const VkDeviceCreateInfo device_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = &features,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queue_info,
			.enabledExtensionCount = 1,
			.ppEnabledExtensionNames = extensions,
		};

		if (!vk::verify(vkCreateDevice(m_adapter_info.physical_device, &device_info, nullptr, &m_device), "vkCreateDevice"))
		{
			return false;
		}

		// Fetch the device functions straight from the driver
		volkLoadDevice(m_device);

		// VMA fetches the functions it needs through these two
		const VmaVulkanFunctions vulkan_functions = {
			.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
			.vkGetDeviceProcAddr = vkGetDeviceProcAddr,
		};

		const VmaAllocatorCreateInfo allocator_info = {
			.physicalDevice = m_adapter_info.physical_device,
			.device = m_device,
			.pVulkanFunctions = &vulkan_functions,
			.instance = m_instance,
			.vulkanApiVersion = VK_API_VERSION_1_3,
		};

		if (!vk::verify(vmaCreateAllocator(&allocator_info, &m_allocator), "vmaCreateAllocator"))
		{
			return false;
		}

		log::info("Device created (Vulkan {}.{})", VK_API_VERSION_MAJOR(m_adapter_info.api_version), VK_API_VERSION_MINOR(m_adapter_info.api_version));
		return true;
	}

	//--------------------------------------------------------------
	bool device::create_instance()
	{
		// SDL knows which instance extensions a window surface needs here
		Uint32 sdl_extension_count = 0;
		const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
		std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_extension_count);

		std::vector<const char*> layers;

		// The validation layer checks every api call and prints readable
		// messages, like the DirectX 12 debug layer. It slows rendering down,
		// so release builds leave it off.
#if !defined(NDEBUG)
		const bool validation = is_validation_layer_available();
		if (validation)
		{
			layers.push_back(k_validation_layer);
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
		else
		{
			log::info("Vulkan validation layer not installed (it comes with the Vulkan SDK), running without it");
		}
#else
		const bool validation = false;
#endif

		const VkApplicationInfo application_info = {
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "buz",
			.pEngineName = "buz",
			.apiVersion = VK_API_VERSION_1_3,
		};

		const VkInstanceCreateInfo instance_info = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &application_info,
			.enabledLayerCount = static_cast<uint32_t>(layers.size()),
			.ppEnabledLayerNames = layers.data(),
			.enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
			.ppEnabledExtensionNames = extensions.data(),
		};

		if (!vk::verify(vkCreateInstance(&instance_info, nullptr, &m_instance), "vkCreateInstance"))
		{
			return false;
		}

		volkLoadInstance(m_instance);

		if (validation)
		{
			const VkDebugUtilsMessengerCreateInfoEXT messenger_info = {
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
				.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
				.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
				.pfnUserCallback = on_validation_message,
			};

			if (vk::verify(vkCreateDebugUtilsMessengerEXT(m_instance, &messenger_info, nullptr, &m_debug_messenger), "vkCreateDebugUtilsMessengerEXT"))
			{
				log::info("Vulkan validation layer enabled");
			}
		}

		return true;
	}

	//--------------------------------------------------------------
	VkInstance device::get_instance() const
	{
		return m_instance;
	}

	//--------------------------------------------------------------
	VkSurfaceKHR device::get_surface() const
	{
		return m_surface;
	}

	//--------------------------------------------------------------
	VkPhysicalDevice device::get_physical_device() const
	{
		return m_adapter_info.physical_device;
	}

	//--------------------------------------------------------------
	VkDevice device::get_vk_device() const
	{
		return m_device;
	}

	//--------------------------------------------------------------
	VmaAllocator device::get_allocator() const
	{
		return m_allocator;
	}

	//--------------------------------------------------------------
	const adapter_info& device::get_adapter_info() const
	{
		return m_adapter_info;
	}
}
