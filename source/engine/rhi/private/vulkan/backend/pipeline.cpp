#include "vulkan/backend/pipeline.h"

#include "vulkan/backend/device.h"

namespace buz::rhi
{
	// rhi::pipeline hands every call to its native object below. Only
	// this backend's files are compiled (see rhi/CMakeLists.txt).

	//--------------------------------------------------------------
	pipeline::pipeline(std::unique_ptr<native> data)
		: m_native(std::move(data))
	{
	}

	//--------------------------------------------------------------
	pipeline::~pipeline() = default;

	//--------------------------------------------------------------
	const pipeline::native& pipeline::get_native() const
	{
		return *m_native;
	}

	//--------------------------------------------------------------
	pipeline::native::native(device::native& owner, VkPipelineLayout layout, VkPipeline vk_pipeline, VkDescriptorSet sampler_set)
		: m_owner(owner)
		, m_layout(layout)
		, m_pipeline(vk_pipeline)
		, m_sampler_set(sampler_set)
	{
		m_owner.resource_created();
	}

	//--------------------------------------------------------------
	pipeline::native::~native()
	{
		device::native& owner = m_owner;
		m_owner.release_later([&owner, layout = m_layout, vk_pipeline = m_pipeline] {
			vkDestroyPipeline(owner.get_vk_device(), vk_pipeline, nullptr);
			vkDestroyPipelineLayout(owner.get_vk_device(), layout, nullptr);
		});
	}

	//--------------------------------------------------------------
	VkPipelineLayout pipeline::native::get_layout() const
	{
		return m_layout;
	}

	//--------------------------------------------------------------
	VkPipeline pipeline::native::get_vk_pipeline() const
	{
		return m_pipeline;
	}

	//--------------------------------------------------------------
	VkDescriptorSet pipeline::native::get_sampler_set() const
	{
		return m_sampler_set;
	}
}
