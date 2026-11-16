#include "vulkan/backend/gpu_texture.h"

#include "vulkan/backend/device.h"
#include "vulkan/descriptor_heap.h"

namespace buz::rhi
{
	// rhi::gpu_texture hands every call to its native object below. Only
	// this backend's files are compiled (see rhi/CMakeLists.txt).

	//--------------------------------------------------------------
	gpu_texture::gpu_texture(uint32_t width, uint32_t height, std::unique_ptr<native> data)
		: texture(width, height)
		, m_native(std::move(data))
	{
	}

	//--------------------------------------------------------------
	gpu_texture::~gpu_texture() = default;

	//--------------------------------------------------------------
	const gpu_texture::native& gpu_texture::get_native() const
	{
		return *m_native;
	}

	//--------------------------------------------------------------
	gpu_texture::native::native(device::native& owner, VkImage image, VmaAllocation allocation, VkImageView view, uint32_t descriptor_index)
		: m_owner(owner)
		, m_image(image)
		, m_allocation(allocation)
		, m_view(view)
		, m_descriptor_index(descriptor_index)
	{
		m_owner.resource_created();
	}

	//--------------------------------------------------------------
	gpu_texture::native::~native()
	{
		// The gpu may still sample this texture in a frame in flight: keep
		// the image, its memory and the descriptor slot until those frames
		// are done.
		device::native& owner = m_owner;
		m_owner.release_later([&owner, image = m_image, allocation = m_allocation, view = m_view, descriptor_index = m_descriptor_index] {
			vkDestroyImageView(owner.get_vk_device(), view, nullptr);
			vmaDestroyImage(owner.get_allocator(), image, allocation);
			owner.get_texture_heap().free(descriptor_index);
		});
	}

	//--------------------------------------------------------------
	uint32_t gpu_texture::native::get_descriptor_index() const
	{
		return m_descriptor_index;
	}
}
