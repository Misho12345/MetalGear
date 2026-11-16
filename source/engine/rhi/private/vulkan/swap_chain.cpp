#include "vulkan/swap_chain.h"

#include "core/log.h"
#include "vulkan/device.h"
#include "vulkan/vk_utils.h"

#include <algorithm>

namespace
{
	//--------------------------------------------------------------
	/// @brief Picks the image format: 8 bits per channel without srgb
	/// conversion, the same the DirectX 12 swap chain uses.
	VkSurfaceFormatKHR choose_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
	{
		uint32_t count = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr);

		std::vector<VkSurfaceFormatKHR> formats(count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats.data());

		for (const VkSurfaceFormatKHR& format : formats)
		{
			if ((format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM) && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				return format;
			}
		}

		return formats.front();
	}

	//--------------------------------------------------------------
	/// @brief Picks how presenting waits: fifo waits for the monitor's
	/// refresh (always available), immediate does not (when supported).
	VkPresentModeKHR choose_present_mode(VkPhysicalDevice physical_device, VkSurfaceKHR surface, buz::vsync wait_for_vsync)
	{
		if (wait_for_vsync)
		{
			return VK_PRESENT_MODE_FIFO_KHR;
		}

		uint32_t count = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr);

		std::vector<VkPresentModeKHR> modes(count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, modes.data());

		const bool has_immediate = std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end();
		return has_immediate ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
	}
}

namespace buz::vulkan
{
	//--------------------------------------------------------------
	swap_chain::swap_chain()
		: m_device(VK_NULL_HANDLE)
		, m_swap_chain(VK_NULL_HANDLE)
		, m_format(VK_FORMAT_UNDEFINED)
		, m_image_index(0)
		, m_width(0)
		, m_height(0)
	{
	}

	//--------------------------------------------------------------
	swap_chain::~swap_chain()
	{
		for (VkImageView view : m_views)
		{
			vkDestroyImageView(m_device, view, nullptr);
		}

		if (m_swap_chain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_device, m_swap_chain, nullptr);
		}
	}

	//--------------------------------------------------------------
	bool swap_chain::initialize(device& graphics_device, uint32_t width, uint32_t height, vsync wait_for_vsync)
	{
		m_device = graphics_device.get_vk_device();
		VkPhysicalDevice physical_device = graphics_device.get_physical_device();
		VkSurfaceKHR surface = graphics_device.get_surface();

		VkSurfaceCapabilitiesKHR capabilities = {};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities);

		// Most windows dictate the image size (their size in pixels); a
		// width of 0xFFFFFFFF means the window leaves it to us.
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			m_width = capabilities.currentExtent.width;
			m_height = capabilities.currentExtent.height;
		}
		else
		{
			m_width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			m_height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		}

		uint32_t image_count = std::max(k_back_buffer_count, capabilities.minImageCount);
		if (capabilities.maxImageCount != 0)
		{
			image_count = std::min(image_count, capabilities.maxImageCount);
		}

		const VkSurfaceFormatKHR surface_format = choose_format(physical_device, surface);
		m_format = surface_format.format;

		const VkSwapchainCreateInfoKHR swap_chain_info = {
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = surface,
			.minImageCount = image_count,
			.imageFormat = surface_format.format,
			.imageColorSpace = surface_format.colorSpace,
			.imageExtent = { m_width, m_height },
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // transfer source: frame captures
			.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.preTransform = capabilities.currentTransform,
			.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode = choose_present_mode(physical_device, surface, wait_for_vsync),
			.clipped = VK_TRUE,
		};

		if (!vk::verify(vkCreateSwapchainKHR(m_device, &swap_chain_info, nullptr, &m_swap_chain), "vkCreateSwapchainKHR"))
		{
			return false;
		}

		uint32_t created_count = 0;
		vkGetSwapchainImagesKHR(m_device, m_swap_chain, &created_count, nullptr);
		m_images.resize(created_count);
		vkGetSwapchainImagesKHR(m_device, m_swap_chain, &created_count, m_images.data());

		// Create a view for every image so we can render into them
		m_views.reserve(m_images.size());
		for (VkImage image : m_images)
		{
			const VkImageViewCreateInfo view_info = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = image,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = m_format,
				.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
			};

			VkImageView view = VK_NULL_HANDLE;
			if (!vk::verify(vkCreateImageView(m_device, &view_info, nullptr, &view), "vkCreateImageView (swap chain)"))
			{
				return false;
			}
			m_views.push_back(view);
		}

		return true;
	}

	//--------------------------------------------------------------
	uint32_t swap_chain::acquire_next_image(VkSemaphore image_ready)
	{
		vk::verify(vkAcquireNextImageKHR(m_device, m_swap_chain, UINT64_MAX, image_ready, VK_NULL_HANDLE, &m_image_index), "vkAcquireNextImageKHR");
		return m_image_index;
	}

	//--------------------------------------------------------------
	bool swap_chain::present(VkQueue queue, VkSemaphore rendering_done)
	{
		const VkPresentInfoKHR present_info = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &rendering_done,
			.swapchainCount = 1,
			.pSwapchains = &m_swap_chain,
			.pImageIndices = &m_image_index,
		};

		return vk::verify(vkQueuePresentKHR(queue, &present_info), "vkQueuePresentKHR");
	}

	//--------------------------------------------------------------
	VkImage swap_chain::get_image(uint32_t index) const
	{
		return m_images[index];
	}

	//--------------------------------------------------------------
	VkImageView swap_chain::get_view(uint32_t index) const
	{
		return m_views[index];
	}

	//--------------------------------------------------------------
	uint32_t swap_chain::get_image_count() const
	{
		return static_cast<uint32_t>(m_images.size());
	}

	//--------------------------------------------------------------
	VkFormat swap_chain::get_format() const
	{
		return m_format;
	}

	//--------------------------------------------------------------
	uint32_t swap_chain::get_width() const
	{
		return m_width;
	}

	//--------------------------------------------------------------
	uint32_t swap_chain::get_height() const
	{
		return m_height;
	}
}
