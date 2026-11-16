#pragma once

#include "core/filesystem.h"

#include <volk.h>

#include <string>

namespace buz::vulkan
{
	//--------------------------------------------------------------
	/// @brief Caches compiled pipelines on disk between runs.
	///
	/// Creating a pipeline makes the driver compile our shader bytecode into
	/// real gpu instructions, which is slow. A VkPipelineCache remembers
	/// those results; it is saved to disk on shutdown and loaded again on
	/// the next start, the Vulkan counterpart of DirectX 12's pipeline
	/// library (d3d12::pso_cache).
	///
	/// The cache invalidates itself: a cache written by another driver or
	/// gpu is rejected when loading, and pipelines are compiled from
	/// scratch (and stored again).
	class pso_cache
	{
	public:
		//----------------------------------------------------------
		/// @brief Creates an empty pso_cache; initialize sets it up.
		pso_cache();
		~pso_cache();

		pso_cache(const pso_cache&) = delete;
		pso_cache& operator=(const pso_cache&) = delete;

		//----------------------------------------------------------
		/// @brief Loads the pipeline cache from disk (or starts an empty one).
		/// @param vk_device Device the pipelines are created on.
		/// @param cache_file File the pipeline cache is stored in.
		/// @return True when the cache is ready for use (an empty cache is also fine).
		bool initialize(VkDevice vk_device, const fs::path& cache_file);

		//----------------------------------------------------------
		/// @brief Creates a graphics pipeline, reusing cached compile results.
		/// @param name Name of the pipeline, used in log messages.
		/// @param desc Full description of the pipeline.
		/// @return Created pipeline, or VK_NULL_HANDLE on failure.
		VkPipeline get_or_create_graphics_pipeline(const std::string& name, const VkGraphicsPipelineCreateInfo& desc);

		//----------------------------------------------------------
		/// @brief Saves the pipeline cache to disk when pipelines were added.
		void save();

	private:
		VkDevice m_device;
		VkPipelineCache m_cache;

		fs::path m_cache_file;
		bool m_dirty;
	};
}
