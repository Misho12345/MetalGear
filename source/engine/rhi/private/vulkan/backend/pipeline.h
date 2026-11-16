#pragma once

#include "rhi/device.h"
#include "rhi/pipeline.h"

#include <volk.h>

namespace buz::rhi
{
	//--------------------------------------------------------------
	/// @brief A pipeline layout (what data the shaders receive) together
	/// with the pipeline (the compiled shaders plus all fixed state). The
	/// DirectX 12 equivalents are the root signature and pipeline state.
	class pipeline::native
	{
	public:
		native(device::native& owner, VkPipelineLayout layout, VkPipeline vk_pipeline, VkDescriptorSet sampler_set);
		~native();

		//----------------------------------------------------------
		/// @brief Returns what data the shaders receive.
		VkPipelineLayout get_layout() const;

		//----------------------------------------------------------
		/// @brief Returns the compiled shaders and fixed state.
		VkPipeline get_vk_pipeline() const;

		//----------------------------------------------------------
		/// @brief Returns the descriptor set holding the pipeline's sampler.
		VkDescriptorSet get_sampler_set() const;

	private:
		device::native& m_owner;
		VkPipelineLayout m_layout;
		VkPipeline m_pipeline;
		VkDescriptorSet m_sampler_set;
	};
}
