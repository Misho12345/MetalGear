#include "vulkan/descriptor_heap.h"

#include "core/log.h"
#include "vulkan/vk_utils.h"

namespace buz::vulkan
{
	//--------------------------------------------------------------
	descriptor_heap::descriptor_heap()
		: m_device(VK_NULL_HANDLE)
		, m_layout(VK_NULL_HANDLE)
		, m_pool(VK_NULL_HANDLE)
		, m_allocated_count(0)
	{
	}

	//--------------------------------------------------------------
	descriptor_heap::~descriptor_heap()
	{
		// Destroying the pool frees every set allocated from it
		if (m_pool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(m_device, m_pool, nullptr);
		}
		if (m_layout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
		}
	}

	//--------------------------------------------------------------
	bool descriptor_heap::initialize(VkDevice vk_device, uint32_t capacity)
	{
		m_device = vk_device;

		// One texture, read by the pixel (fragment) shader
		const VkDescriptorSetLayoutBinding binding = {
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		};

		const VkDescriptorSetLayoutCreateInfo layout_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = 1,
			.pBindings = &binding,
		};

		if (!vk::verify(vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr, &m_layout), "vkCreateDescriptorSetLayout"))
		{
			return false;
		}

		const VkDescriptorPoolSize pool_size = { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = capacity };

		const VkDescriptorPoolCreateInfo pool_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = capacity,
			.poolSizeCount = 1,
			.pPoolSizes = &pool_size,
		};

		if (!vk::verify(vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &m_pool), "vkCreateDescriptorPool"))
		{
			return false;
		}

		// Every slot gets its set right away; a slot is reused, never freed
		const std::vector<VkDescriptorSetLayout> layouts(capacity, m_layout);
		m_sets.resize(capacity);

		const VkDescriptorSetAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = m_pool,
			.descriptorSetCount = capacity,
			.pSetLayouts = layouts.data(),
		};

		return vk::verify(vkAllocateDescriptorSets(vk_device, &allocate_info, m_sets.data()), "vkAllocateDescriptorSets");
	}

	//--------------------------------------------------------------
	uint32_t descriptor_heap::allocate()
	{
		if (!m_free_slots.empty())
		{
			const uint32_t index = m_free_slots.back();
			m_free_slots.pop_back();
			return index;
		}

		if (m_allocated_count >= m_sets.size())
		{
			log::error("Descriptor heap is full (capacity: {})", m_sets.size());
			return 0;
		}

		return m_allocated_count++;
	}

	//--------------------------------------------------------------
	void descriptor_heap::write_texture(uint32_t index, VkImageView view)
	{
		const VkDescriptorImageInfo image_info = {
			.imageView = view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		const VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_sets[index],
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.pImageInfo = &image_info,
		};

		vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
	}

	//--------------------------------------------------------------
	void descriptor_heap::free(uint32_t index)
	{
		m_free_slots.push_back(index);
	}

	//--------------------------------------------------------------
	VkDescriptorSet descriptor_heap::get_set(uint32_t index) const
	{
		return m_sets[index];
	}

	//--------------------------------------------------------------
	VkDescriptorSetLayout descriptor_heap::get_layout() const
	{
		return m_layout;
	}
}
