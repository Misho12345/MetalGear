#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace buz::vulkan
{
	//--------------------------------------------------------------
	/// @brief A fixed number of texture descriptors, handed out one slot at
	/// a time: the Vulkan counterpart of d3d12::descriptor_heap.
	///
	/// A descriptor tells a shader where a resource lives. Vulkan groups
	/// descriptors in descriptor sets; this heap creates one set per slot
	/// up front, each holding a single texture (binding 0 of set 0 in the
	/// shaders). Freed slots are handed out again.
	class descriptor_heap
	{
	public:
		//----------------------------------------------------------
		/// @brief Creates an empty descriptor_heap; initialize sets it up.
		descriptor_heap();
		~descriptor_heap();

		descriptor_heap(const descriptor_heap&) = delete;
		descriptor_heap& operator=(const descriptor_heap&) = delete;

		//----------------------------------------------------------
		/// @brief Creates the set layout, the pool and every set.
		/// @param vk_device Device used to create them.
		/// @param capacity Maximum number of textures.
		/// @return True when the heap was created successfully.
		bool initialize(VkDevice vk_device, uint32_t capacity);

		//----------------------------------------------------------
		/// @brief Reserves a free descriptor slot.
		/// @return Index of the reserved slot.
		uint32_t allocate();

		//----------------------------------------------------------
		/// @brief Gives a slot back so allocate can hand it out again.
		/// @param index Slot index returned by allocate().
		void free(uint32_t index);

		//----------------------------------------------------------
		/// @brief Points a slot's descriptor at a texture.
		/// @param index Slot index returned by allocate().
		/// @param view View of the texture, in the shader read layout.
		void write_texture(uint32_t index, VkImageView view);

		//----------------------------------------------------------
		/// @brief Returns the descriptor set of a slot (used while drawing).
		VkDescriptorSet get_set(uint32_t index) const;

		//----------------------------------------------------------
		/// @brief Returns the layout every set has; pipelines are built with it.
		VkDescriptorSetLayout get_layout() const;

	private:
		VkDevice m_device;
		VkDescriptorSetLayout m_layout;
		VkDescriptorPool m_pool;
		std::vector<VkDescriptorSet> m_sets;

		uint32_t m_allocated_count;
		std::vector<uint32_t> m_free_slots;
	};
}
