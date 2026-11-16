#pragma once

#include "rhi/buffer.h"
#include "rhi/device.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>

namespace buz::rhi
{
	//--------------------------------------------------------------
	/// @brief A vertex or index buffer.
	///
	/// Buffers live in cpu memory the gpu reads directly (like the DirectX
	/// 12 upload heap). That keeps them simple and is plenty fast for the
	/// small buffers buz draws with; big meshes would be copied to gpu
	/// memory, like textures.
	class buffer::native
	{
	public:
		native(device::native& owner, VkBuffer vk_buffer, VmaAllocation allocation, buffer_usage usage, uint32_t stride);
		~native();

		//----------------------------------------------------------
		/// @brief Returns what the buffer holds.
		buffer_usage get_usage() const;

		//----------------------------------------------------------
		/// @brief Returns the Vulkan buffer.
		VkBuffer get_vk_buffer() const;

		//----------------------------------------------------------
		/// @brief Returns the size of one vertex in bytes.
		uint32_t get_stride() const;

	private:
		device::native& m_owner;
		VkBuffer m_buffer;
		VmaAllocation m_allocation;
		buffer_usage m_usage;
		uint32_t m_stride;
	};
}
