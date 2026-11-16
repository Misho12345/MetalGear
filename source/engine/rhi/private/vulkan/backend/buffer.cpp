#include "vulkan/backend/buffer.h"

#include "vulkan/backend/device.h"

namespace buz::rhi
{
	// rhi::buffer hands every call to its native object below. Only
	// this backend's files are compiled (see rhi/CMakeLists.txt).

	//--------------------------------------------------------------
	buffer::buffer(std::unique_ptr<native> data)
		: m_native(std::move(data))
	{
	}

	//--------------------------------------------------------------
	buffer::~buffer() = default;

	//--------------------------------------------------------------
	const buffer::native& buffer::get_native() const
	{
		return *m_native;
	}

	//--------------------------------------------------------------
	buffer::native::native(device::native& owner, VkBuffer vk_buffer, VmaAllocation allocation, buffer_usage usage, uint32_t stride)
		: m_owner(owner)
		, m_buffer(vk_buffer)
		, m_allocation(allocation)
		, m_usage(usage)
		, m_stride(stride)
	{
		m_owner.resource_created();
	}

	//--------------------------------------------------------------
	buffer::native::~native()
	{
		device::native& owner = m_owner;
		m_owner.release_later([&owner, vk_buffer = m_buffer, allocation = m_allocation] {
			vmaDestroyBuffer(owner.get_allocator(), vk_buffer, allocation);
		});
	}

	//--------------------------------------------------------------
	buffer_usage buffer::native::get_usage() const
	{
		return m_usage;
	}

	//--------------------------------------------------------------
	VkBuffer buffer::native::get_vk_buffer() const
	{
		return m_buffer;
	}

	//--------------------------------------------------------------
	uint32_t buffer::native::get_stride() const
	{
		return m_stride;
	}
}
