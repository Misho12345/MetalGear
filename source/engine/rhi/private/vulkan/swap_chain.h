#pragma once

#include "rhi/settings.h"

#include <volk.h>

#include <cstdint>
#include <vector>

namespace buz::vulkan
{
	class device;

	//--------------------------------------------------------------
	/// @brief Owns the swap chain and its images.
	///
	/// The swap chain holds the images that end up on screen. While the gpu
	/// renders into one image, another one is being displayed. Unlike
	/// DirectX, Vulkan has to be asked which image comes next
	/// (acquire_next_image): it returns the index right away and signals a
	/// semaphore once that image is free.
	class swap_chain
	{
	public:
		// At least two images: one on screen, one being rendered into
		static constexpr uint32_t k_back_buffer_count = 2;

		//----------------------------------------------------------
		/// @brief Creates an empty swap_chain; initialize sets it up.
		swap_chain();
		~swap_chain();

		swap_chain(const swap_chain&) = delete;
		swap_chain& operator=(const swap_chain&) = delete;

		//----------------------------------------------------------
		/// @brief Creates the swap chain for the device's window surface.
		/// @param graphics_device Device the images are created on.
		/// @param width Width of the images in pixels, when the window leaves it open.
		/// @param height Height of the images in pixels, when the window leaves it open.
		/// @param wait_for_vsync Whether present waits for the monitor's refresh.
		/// @return True when the swap chain was created successfully.
		bool initialize(device& graphics_device, uint32_t width, uint32_t height, rhi::vsync wait_for_vsync);

		//----------------------------------------------------------
		/// @brief Asks for the image to render into this frame.
		/// @param image_ready Semaphore signalled once the image is free;
		/// the submit that draws into it waits for this.
		/// @return Index of the image.
		uint32_t acquire_next_image(VkSemaphore image_ready);

		//----------------------------------------------------------
		/// @brief Returns a swap chain image.
		VkImage get_image(uint32_t index) const;

		//----------------------------------------------------------
		/// @brief Returns the view used to render into a swap chain image.
		VkImageView get_view(uint32_t index) const;

		//----------------------------------------------------------
		/// @brief Returns how many images the swap chain has.
		uint32_t get_image_count() const;

		//----------------------------------------------------------
		/// @brief Returns the pixel format of the images; pipelines need it.
		VkFormat get_format() const;

		//----------------------------------------------------------
		/// @brief Shows the image acquired this frame on screen.
		/// @param queue Queue that presents.
		/// @param rendering_done Semaphore the drawing signals when finished.
		/// @return True when presenting succeeded.
		bool present(VkQueue queue, VkSemaphore rendering_done);

		//----------------------------------------------------------
		/// @brief Returns the width of the images in pixels.
		uint32_t get_width() const;

		//----------------------------------------------------------
		/// @brief Returns the height of the images in pixels.
		uint32_t get_height() const;

	private:
		VkDevice m_device;                // not owned: the device the swap chain was made on
		VkSwapchainKHR m_swap_chain;      // shows the images in the window
		std::vector<VkImage> m_images;    // drawn into, one at a time
		std::vector<VkImageView> m_views; // one per image
		VkFormat m_format;                // the pixel format of the images
		uint32_t m_image_index;           // the image acquired last

		uint32_t m_width;  // of the images, in pixels
		uint32_t m_height; // of the images, in pixels
	};
}
