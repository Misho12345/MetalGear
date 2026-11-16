#include "vulkan/pso_cache.h"

#include "core/log.h"
#include "vulkan/vk_utils.h"

#include <fstream>
#include <vector>

namespace
{
	//--------------------------------------------------------------
	/// @brief Reads a whole file into a byte buffer.
	std::vector<char> read_file(const buz::fs::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			return {};
		}

		const std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);

		std::vector<char> data(static_cast<size_t>(size));
		file.read(data.data(), size);

		return data;
	}
}

namespace buz::vulkan
{
	//--------------------------------------------------------------
	pso_cache::pso_cache()
		: m_device(VK_NULL_HANDLE)
		, m_cache(VK_NULL_HANDLE)
		, m_dirty(false)
	{
	}

	//--------------------------------------------------------------
	pso_cache::~pso_cache()
	{
		if (m_cache != VK_NULL_HANDLE)
		{
			vkDestroyPipelineCache(m_device, m_cache, nullptr);
		}
	}

	//--------------------------------------------------------------
	bool pso_cache::initialize(VkDevice vk_device, const fs::path& cache_file)
	{
		m_device = vk_device;
		m_cache_file = cache_file;

		// Try to load the cache written by a previous run. The driver checks
		// the header (vendor, device, driver version) and rejects data that
		// is not its own.
		const std::vector<char> data = read_file(cache_file);
		if (!data.empty())
		{
			const VkPipelineCacheCreateInfo cache_info = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
				.initialDataSize = data.size(),
				.pInitialData = data.data(),
			};

			if (vkCreatePipelineCache(vk_device, &cache_info, nullptr, &m_cache) == VK_SUCCESS)
			{
				log::info("Loaded pso cache \"{}\" ({} bytes)", cache_file.string(), data.size());
				return true;
			}

			log::warn("Pso cache \"{}\" is unusable, deleting it and rebuilding", cache_file.string());
			fs::remove(cache_file);
		}

		// No usable cache on disk (first run, or the driver/gpu changed
		// since the cache was written): start with an empty one.
		const VkPipelineCacheCreateInfo empty_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
		if (!vk::verify(vkCreatePipelineCache(vk_device, &empty_info, nullptr, &m_cache), "vkCreatePipelineCache"))
		{
			log::warn("Failed to create a pipeline cache, the pso cache is disabled");
			m_cache = VK_NULL_HANDLE;
		}

		return true;
	}

	//--------------------------------------------------------------
	VkPipeline pso_cache::get_or_create_graphics_pipeline(const std::string& name, const VkGraphicsPipelineCreateInfo& desc)
	{
		VkPipeline pipeline = VK_NULL_HANDLE;
		if (!vk::verify(vkCreateGraphicsPipelines(m_device, m_cache, 1, &desc, nullptr, &pipeline), "vkCreateGraphicsPipelines"))
		{
			return VK_NULL_HANDLE;
		}

		// Vulkan does not say whether the cache had the pipeline already, so
		// unlike DirectX 12 there is only one message here.
		log::info("Pipeline \"{}\" created", name);

		m_dirty = m_cache != VK_NULL_HANDLE;
		return pipeline;
	}

	//--------------------------------------------------------------
	void pso_cache::save()
	{
		if (m_cache == VK_NULL_HANDLE || !m_dirty)
		{
			return;
		}

		size_t size = 0;
		vkGetPipelineCacheData(m_device, m_cache, &size, nullptr);

		std::vector<char> data(size);
		if (!vk::verify(vkGetPipelineCacheData(m_device, m_cache, &size, data.data()), "vkGetPipelineCacheData"))
		{
			return;
		}

		fs::create_directories(m_cache_file.parent_path());

		std::ofstream file(m_cache_file, std::ios::binary);
		if (!file.is_open())
		{
			log::error("Failed to write pso cache \"{}\"", m_cache_file.string());
			return;
		}

		file.write(data.data(), static_cast<std::streamsize>(size));
		log::info("Saved pso cache \"{}\" ({} bytes)", m_cache_file.string(), size);

		m_dirty = false;
	}
}
