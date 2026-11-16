#pragma once

#include "rhi/device.h"
#include "rhi/gpu_texture.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>

namespace buz::rhi
{
	//--------------------------------------------------------------
	/// @brief A texture in gpu memory plus its descriptor (what shaders
	/// use to sample it).
	class gpu_texture::native
	{
	public:
		native(device::native& owner, VkImage image, VmaAllocation allocation, VkImageView view, uint32_t descriptor_index);
		~native();

		//----------------------------------------------------------
		/// @brief Returns the slot of the texture descriptor in the texture heap.
		uint32_t get_descriptor_index() const;

	private:
		device::native& m_owner;
		VkImage m_image;
		VmaAllocation m_allocation;
		VkImageView m_view;
		uint32_t m_descriptor_index;
	};
}
