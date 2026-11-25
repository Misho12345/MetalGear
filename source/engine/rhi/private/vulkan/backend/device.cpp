#include "vulkan/backend/device.h"

#include "core/assert.h"
#include "core/log.h"
#include "rhi/graphics_api.h"
#include "vulkan/backend/buffer.h"
#include "vulkan/backend/command_list.h"
#include "vulkan/backend/gpu_texture.h"
#include "vulkan/backend/pipeline.h"
#include "vulkan/vk_utils.h"

#include <cstring>
#include <vector>

namespace
{
	// 32 bit float depth: enough precision, and a format both the DirectX 12
	// and the Vulkan backend support
	constexpr VkFormat k_depth_format = VK_FORMAT_D32_SFLOAT;

	//--------------------------------------------------------------
	/// @brief Converts an rhi vertex format to the matching Vulkan format.
	VkFormat to_vk_format(buz::rhi::vertex_format format)
	{
		switch (format)
		{
		case buz::rhi::vertex_format::float2: return VK_FORMAT_R32G32_SFLOAT;
		case buz::rhi::vertex_format::float3: return VK_FORMAT_R32G32B32_SFLOAT;
		case buz::rhi::vertex_format::float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
		}
		return VK_FORMAT_UNDEFINED;
	}

	//--------------------------------------------------------------
	/// @brief Creates a shader module from compiled SPIR-V.
	VkShaderModule create_shader_module(VkDevice vk_device, std::span<const std::byte> spirv)
	{
		const VkShaderModuleCreateInfo module_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = spirv.size(),
			.pCode = reinterpret_cast<const uint32_t*>(spirv.data()),
		};

		VkShaderModule shader_module = VK_NULL_HANDLE;
		buz::vk::verify(vkCreateShaderModule(vk_device, &module_info, nullptr, &shader_module), "vkCreateShaderModule");
		return shader_module;
	}
}

namespace buz::rhi
{
	// rhi::device hands every call to its native object below. Only
	// this backend's files are compiled (see rhi/CMakeLists.txt).

	//--------------------------------------------------------------
	std::unique_ptr<device> device::create(const device_desc& desc)
	{
		std::unique_ptr<native> created = std::make_unique<native>();
		if (!created->initialize(desc))
		{
			return nullptr;
		}

		return std::make_unique<device>(std::move(created));
	}

	//--------------------------------------------------------------
	device::device(std::unique_ptr<native> data)
		: m_native(std::move(data))
	{
	}

	//--------------------------------------------------------------
	device::~device() = default;

	//--------------------------------------------------------------
	std::unique_ptr<gpu_texture> device::create_texture(uint32_t width, uint32_t height, std::span<const uint8_t> rgba_pixels)
	{
		return m_native->create_texture(width, height, rgba_pixels);
	}

	//--------------------------------------------------------------
	std::unique_ptr<buffer> device::create_buffer(buffer_usage usage, std::span<const std::byte> data, uint32_t stride)
	{
		return m_native->create_buffer(usage, data, stride);
	}

	//--------------------------------------------------------------
	std::unique_ptr<pipeline> device::create_pipeline(const pipeline_desc& desc)
	{
		return m_native->create_pipeline(desc);
	}

	//--------------------------------------------------------------
	command_list& device::begin_frame(color clear_color)
	{
		return m_native->begin_frame(clear_color);
	}

	//--------------------------------------------------------------
	void device::end_frame()
	{
		m_native->end_frame();
	}

	//--------------------------------------------------------------
	void device::shutdown()
	{
		m_native->shutdown();
	}

	//--------------------------------------------------------------
	frame_stats device::get_frame_stats() const
	{
		return m_native->get_frame_stats();
	}

	//--------------------------------------------------------------
	void device::request_capture()
	{
		m_native->request_capture();
	}

	//--------------------------------------------------------------
	std::vector<uint8_t> device::read_capture()
	{
		return m_native->read_capture();
	}

	//--------------------------------------------------------------
	uint32_t device::get_width() const
	{
		return m_native->get_width();
	}

	//--------------------------------------------------------------
	uint32_t device::get_height() const
	{
		return m_native->get_height();
	}

	//--------------------------------------------------------------
	device::native::native()
		: m_depth_image(VK_NULL_HANDLE)
		, m_depth_allocation(VK_NULL_HANDLE)
		, m_depth_view(VK_NULL_HANDLE)
		, m_sampler_pool(VK_NULL_HANDLE)
		, m_data_layout(VK_NULL_HANDLE)
		, m_data_pool(VK_NULL_HANDLE)
		, m_frame_index(0)
		, m_image_index(0)
		, m_command_list(std::make_unique<command_list::native>())
		, m_capture_requested(false)
		, m_capture_buffer(VK_NULL_HANDLE)
		, m_capture_allocation(VK_NULL_HANDLE)
		, m_capture_data(nullptr)
		, m_capture_fence_value(0)
		, m_upload_pool(VK_NULL_HANDLE)
		, m_upload_command_buffer(VK_NULL_HANDLE)
		, m_is_shut_down(false)
	{
	}

	//--------------------------------------------------------------
	graphics_api get_graphics_api()
	{
		return graphics_api::vulkan;
	}

	//--------------------------------------------------------------
	const char* device::get_shader_extension() const
	{
		return ".spv";
	}

	//--------------------------------------------------------------
	device::native::~native()
	{
		if (!m_is_shut_down)
		{
			shutdown();
		}

		// Vulkan objects are not reference counted like DirectX 12's: the
		// ones the backend owns directly are destroyed here, before the
		// members (and finally the device itself) go.
		VkDevice vk_device = m_device.get_vk_device();
		if (vk_device == VK_NULL_HANDLE)
		{
			return;
		}

		for (frame& frame_data : m_frames)
		{
			vkDestroySemaphore(vk_device, frame_data.image_acquired, nullptr);
			vkDestroyCommandPool(vk_device, frame_data.command_pool, nullptr);
			vmaDestroyBuffer(m_device.get_allocator(), frame_data.scratch_buffer, frame_data.scratch_allocation);
		}

		for (VkSemaphore semaphore : m_rendering_done)
		{
			vkDestroySemaphore(vk_device, semaphore, nullptr);
		}

		vkDestroyCommandPool(vk_device, m_upload_pool, nullptr);
		vkDestroyImageView(vk_device, m_depth_view, nullptr);
		vmaDestroyImage(m_device.get_allocator(), m_depth_image, m_depth_allocation);
		vmaDestroyBuffer(m_device.get_allocator(), m_capture_buffer, m_capture_allocation);

		for (sampler_set& samplers : m_sampler_sets)
		{
			vkDestroyDescriptorSetLayout(vk_device, samplers.layout, nullptr);
			vkDestroySampler(vk_device, samplers.sampler, nullptr);
		}

		vkDestroyDescriptorPool(vk_device, m_sampler_pool, nullptr);

		vkDestroyDescriptorPool(vk_device, m_data_pool, nullptr);
		vkDestroyDescriptorSetLayout(vk_device, m_data_layout, nullptr);
	}

	//--------------------------------------------------------------
	bool device::native::initialize(const device_desc& desc)
	{
		log::info("Graphics api: Vulkan");

		if (!m_device.initialize(desc.window, desc.gpu_preference))
		{
			return false;
		}

		VkDevice vk_device = m_device.get_vk_device();

		if (!m_queue.initialize(vk_device, m_device.get_adapter_info().graphics_queue_family))
		{
			return false;
		}

		if (!m_swap_chain.initialize(m_device, desc.width, desc.height, desc.use_vsync))
		{
			return false;
		}

		// The depth image and its view
		const VkImageCreateInfo depth_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = k_depth_format,
			.extent = { m_swap_chain.get_width(), m_swap_chain.get_height(), 1 },
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};
		const VmaAllocationCreateInfo depth_allocation_info = { .usage = VMA_MEMORY_USAGE_AUTO };
		if (!vk::verify(vmaCreateImage(m_device.get_allocator(), &depth_info, &depth_allocation_info, &m_depth_image, &m_depth_allocation, nullptr), "vmaCreateImage (depth)"))
		{
			return false;
		}

		const VkImageViewCreateInfo depth_view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = m_depth_image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = k_depth_format,
			.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 },
		};
		if (!vk::verify(vkCreateImageView(vk_device, &depth_view_info, nullptr, &m_depth_view), "vkCreateImageView (depth)"))
		{
			return false;
		}

		// One heap holds the descriptors of all textures
		if (!m_texture_heap.initialize(vk_device, k_max_textures))
		{
			return false;
		}

		if (!m_pso_cache.initialize(vk_device, desc.cache_directory / "pso_cache_vulkan.bin"))
		{
			return false;
		}

		if (!create_sampler_sets())
		{
			return false;
		}

		// Every frame in flight records into its own command pool and
		// buffer, and gets its own semaphore to learn when its swap chain
		// image is free.
		const VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		for (frame& frame_data : m_frames)
		{
			if (!create_command_buffer(frame_data.command_pool, frame_data.command_buffer))
			{
				return false;
			}

			if (!vk::verify(vkCreateSemaphore(vk_device, &semaphore_info, nullptr, &frame_data.image_acquired), "vkCreateSemaphore"))
			{
				return false;
			}

			// The frame's scratch memory: a host visible buffer, mapped once
			// and written for as long as the backend lives. It holds vertex
			// data (set_vertex_data) and the shader and frame data
			// (set_shader_data, set_frame_data), which shaders read as
			// uniform buffers.
			const VkBufferCreateInfo scratch_info = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = k_frame_scratch_size,
				.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			};

			const VmaAllocationCreateInfo scratch_allocation_info = {
				.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.usage = VMA_MEMORY_USAGE_AUTO,
			};

			VmaAllocationInfo scratch_mapping = {};
			if (!vk::verify(vmaCreateBuffer(m_device.get_allocator(), &scratch_info, &scratch_allocation_info, &frame_data.scratch_buffer, &frame_data.scratch_allocation, &scratch_mapping), "vmaCreateBuffer (frame scratch)"))
			{
				return false;
			}
			frame_data.scratch_data = static_cast<std::byte*>(scratch_mapping.pMappedData);
		}

		if (!create_data_sets())
		{
			return false;
		}

		m_rendering_done.resize(m_swap_chain.get_image_count(), VK_NULL_HANDLE);
		for (VkSemaphore& semaphore : m_rendering_done)
		{
			if (!vk::verify(vkCreateSemaphore(vk_device, &semaphore_info, nullptr, &semaphore), "vkCreateSemaphore"))
			{
				return false;
			}
		}

		return create_command_buffer(m_upload_pool, m_upload_command_buffer);
	}

	//--------------------------------------------------------------
	bool device::native::create_command_buffer(VkCommandPool& out_pool, VkCommandBuffer& out_command_buffer)
	{
		VkDevice vk_device = m_device.get_vk_device();

		const VkCommandPoolCreateInfo pool_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = m_device.get_adapter_info().graphics_queue_family,
		};

		if (!vk::verify(vkCreateCommandPool(vk_device, &pool_info, nullptr, &out_pool), "vkCreateCommandPool"))
		{
			return false;
		}

		const VkCommandBufferAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = out_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};

		return vk::verify(vkAllocateCommandBuffers(vk_device, &allocate_info, &out_command_buffer), "vkAllocateCommandBuffers");
	}

	//--------------------------------------------------------------
	bool device::native::create_sampler_sets()
	{
		VkDevice vk_device = m_device.get_vk_device();

		const VkDescriptorPoolSize pool_size = { .type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 2 };
		const VkDescriptorPoolCreateInfo pool_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = 2,
			.poolSizeCount = 1,
			.pPoolSizes = &pool_size,
		};

		if (!vk::verify(vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &m_sampler_pool), "vkCreateDescriptorPool (samplers)"))
		{
			return false;
		}

		const sampler_filter filters[] = { sampler_filter::linear, sampler_filter::nearest };
		for (const sampler_filter filter : filters)
		{
			sampler_set& samplers = m_sampler_sets[static_cast<size_t>(filter)];
			const VkFilter vk_filter = filter == sampler_filter::nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

			const VkSamplerCreateInfo sampler_info = {
				.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
				.magFilter = vk_filter,
				.minFilter = vk_filter,
				.mipmapMode = filter == sampler_filter::nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR,
				.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.maxLod = VK_LOD_CLAMP_NONE,
			};

			if (!vk::verify(vkCreateSampler(vk_device, &sampler_info, nullptr, &samplers.sampler), "vkCreateSampler"))
			{
				return false;
			}

			// An immutable sampler is part of the set layout itself, just
			// like a static sampler is part of a root signature: the set
			// needs no further writes.
			const VkDescriptorSetLayoutBinding binding = {
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.pImmutableSamplers = &samplers.sampler,
			};

			const VkDescriptorSetLayoutCreateInfo layout_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = 1,
				.pBindings = &binding,
			};

			if (!vk::verify(vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr, &samplers.layout), "vkCreateDescriptorSetLayout (sampler)"))
			{
				return false;
			}

			const VkDescriptorSetAllocateInfo allocate_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = m_sampler_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &samplers.layout,
			};

			if (!vk::verify(vkAllocateDescriptorSets(vk_device, &allocate_info, &samplers.set), "vkAllocateDescriptorSets (sampler)"))
			{
				return false;
			}
		}

		return true;
	}

	//--------------------------------------------------------------
	bool device::native::create_data_sets()
	{
		VkDevice vk_device = m_device.get_vk_device();

		// A dynamic uniform buffer: the set is written once, and every bind
		// passes the offset of the data in the frame's scratch buffer. The
		// same layout serves set 2 (shader data) and set 3 (frame data),
		// the root parameters 2 and 3 of the DirectX 12 backend.
		const VkDescriptorSetLayoutBinding binding = {
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		};

		const VkDescriptorSetLayoutCreateInfo layout_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = 1,
			.pBindings = &binding,
		};

		if (!vk::verify(vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr, &m_data_layout), "vkCreateDescriptorSetLayout (data)"))
		{
			return false;
		}

		constexpr uint32_t set_count = k_frames_in_flight * k_data_slot_count;
		const VkDescriptorPoolSize pool_size = { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .descriptorCount = set_count };
		const VkDescriptorPoolCreateInfo pool_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = set_count,
			.poolSizeCount = 1,
			.pPoolSizes = &pool_size,
		};

		if (!vk::verify(vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &m_data_pool), "vkCreateDescriptorPool (data)"))
		{
			return false;
		}

		for (frame& frame_data : m_frames)
		{
			for (uint32_t slot = 0; slot < k_data_slot_count; ++slot)
			{
				const VkDescriptorSetAllocateInfo allocate_info = {
					.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
					.descriptorPool = m_data_pool,
					.descriptorSetCount = 1,
					.pSetLayouts = &m_data_layout,
				};

				if (!vk::verify(vkAllocateDescriptorSets(vk_device, &allocate_info, &frame_data.data_sets[slot]), "vkAllocateDescriptorSets (data)"))
				{
					return false;
				}

				// The slot's full size (see get_data_size), so a shader may
				// declare a block of any size up to it
				const VkDescriptorBufferInfo buffer_info = {
					.buffer = frame_data.scratch_buffer,
					.offset = 0,
					.range = get_data_size(static_cast<data_slot>(slot)),
				};

				const VkWriteDescriptorSet write = {
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = frame_data.data_sets[slot],
					.dstBinding = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
					.pBufferInfo = &buffer_info,
				};
				vkUpdateDescriptorSets(vk_device, 1, &write, 0, nullptr);
			}
		}

		return true;
	}

	//--------------------------------------------------------------
	std::unique_ptr<gpu_texture> device::native::create_texture(uint32_t width, uint32_t height, std::span<const uint8_t> rgba_pixels)
	{
		// The span knows how many bytes the caller handed over, so a buffer
		// that does not match the promised size is caught right here instead
		// of reading out of bounds during the copy below.
		[[maybe_unused]] const size_t expected_size = static_cast<size_t>(width) * height * 4;
		BUZ_ASSERT_MSG(rgba_pixels.size() == expected_size, "pixel data is {} bytes, but a {}x{} rgba texture needs {}", rgba_pixels.size(), width, height, expected_size);

		VkDevice vk_device = m_device.get_vk_device();
		VmaAllocator allocator = m_device.get_allocator();

		// Create the texture image in gpu memory
		const VkImageCreateInfo image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.extent = { width, height, 1 },
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};

		const VmaAllocationCreateInfo image_allocation_info = { .usage = VMA_MEMORY_USAGE_AUTO };

		VkImage image = VK_NULL_HANDLE;
		VmaAllocation image_allocation = VK_NULL_HANDLE;
		if (!vk::verify(vmaCreateImage(allocator, &image_info, &image_allocation_info, &image, &image_allocation, nullptr), "vmaCreateImage (texture)"))
		{
			return nullptr;
		}

		// Create the staging buffer (cpu-writable memory the gpu can copy
		// from; the upload buffer of the DirectX 12 backend) and fill it.
		// Vulkan takes the rows tightly packed, no padding needed.
		const VkBufferCreateInfo staging_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = rgba_pixels.size(),
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		};

		const VmaAllocationCreateInfo staging_allocation_info = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VmaAllocation staging_allocation = VK_NULL_HANDLE;
		VmaAllocationInfo staging_mapping = {};
		if (!vk::verify(vmaCreateBuffer(allocator, &staging_info, &staging_allocation_info, &staging_buffer, &staging_allocation, &staging_mapping), "vmaCreateBuffer (staging)"))
		{
			vmaDestroyImage(allocator, image, image_allocation);
			return nullptr;
		}

		std::memcpy(staging_mapping.pMappedData, rgba_pixels.data(), rgba_pixels.size());

		// Record the gpu copy from the staging buffer into the texture
		vkResetCommandPool(vk_device, m_upload_pool, 0);

		const VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		vkBeginCommandBuffer(m_upload_command_buffer, &begin_info);

		// Make the image ready to be copied into...
		vk::record_barrier(m_upload_command_buffer, vk::make_image_barrier(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT));

		const VkBufferImageCopy copy_region = {
			.imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
			.imageExtent = { width, height, 1 },
		};
		vkCmdCopyBufferToImage(m_upload_command_buffer, staging_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

		// ...then readable by pixel shaders
		vk::record_barrier(m_upload_command_buffer, vk::make_image_barrier(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT));

		vkEndCommandBuffer(m_upload_command_buffer);

		// Wait until the copy finished; only then may the staging buffer go
		m_queue.wait_for_fence_value(m_queue.submit(m_upload_command_buffer));
		vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);

		// Create the view and the descriptor so shaders can sample the texture
		const VkImageViewCreateInfo view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = image_info.format,
			.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
		};

		VkImageView view = VK_NULL_HANDLE;
		if (!vk::verify(vkCreateImageView(vk_device, &view_info, nullptr, &view), "vkCreateImageView (texture)"))
		{
			vmaDestroyImage(allocator, image, image_allocation);
			return nullptr;
		}

		const uint32_t descriptor_index = m_texture_heap.allocate();
		m_texture_heap.write_texture(descriptor_index, view);

		return std::make_unique<gpu_texture>(width, height, std::make_unique<gpu_texture::native>(*this, image, image_allocation, view, descriptor_index));
	}

	//--------------------------------------------------------------
	std::unique_ptr<buffer> device::native::create_buffer(buffer_usage usage, std::span<const std::byte> data, uint32_t stride)
	{
		const VkBufferCreateInfo buffer_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = data.size(),
			.usage = static_cast<VkBufferUsageFlags>(usage == buffer_usage::vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
		};

		const VmaAllocationCreateInfo allocation_info = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		VkBuffer vk_buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VmaAllocationInfo mapping = {};
		if (!vk::verify(vmaCreateBuffer(m_device.get_allocator(), &buffer_info, &allocation_info, &vk_buffer, &allocation, &mapping), "vmaCreateBuffer"))
		{
			return nullptr;
		}

		std::memcpy(mapping.pMappedData, data.data(), data.size());

		return std::make_unique<buffer>(std::make_unique<buffer::native>(*this, vk_buffer, allocation, usage, stride));
	}

	//--------------------------------------------------------------
	std::unique_ptr<pipeline> device::native::create_pipeline(const pipeline_desc& desc)
	{
		[[maybe_unused]] const bool constants_fit = desc.constants_size <= k_max_constants_size && desc.constants_size % 4 == 0;
		BUZ_ASSERT_MSG(constants_fit, "pipeline \"{}\": constants must be a multiple of 4 bytes and at most {} bytes, not {}", desc.name, k_max_constants_size, desc.constants_size);

		if (desc.vertex_shader.empty() || desc.pixel_shader.empty())
		{
			log::error("Pipeline \"{}\" is missing a shader", desc.name);
			return nullptr;
		}

		VkDevice vk_device = m_device.get_vk_device();
		const sampler_set& samplers = m_sampler_sets[static_cast<size_t>(desc.filter)];

		// The pipeline layout: set 0 holds the texture, set 1 the sampler,
		// set 2 the shader data and set 3 the frame data, and the constants
		// are push constants. The same layout as the root signature of the
		// DirectX 12 backend (b0, t0, s0, b1, b2).
		const VkDescriptorSetLayout set_layouts[] = { m_texture_heap.get_layout(), samplers.layout, m_data_layout, m_data_layout };
		const VkPushConstantRange constants_range = {
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0,
			.size = desc.constants_size,
		};

		const VkPipelineLayoutCreateInfo layout_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = static_cast<uint32_t>(std::size(set_layouts)),
			.pSetLayouts = set_layouts,
			.pushConstantRangeCount = desc.constants_size > 0 ? 1u : 0u,
			.pPushConstantRanges = &constants_range,
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;
		if (!vk::verify(vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &layout), "vkCreatePipelineLayout"))
		{
			return nullptr;
		}

		const VkShaderModule vertex_module = create_shader_module(vk_device, desc.vertex_shader);
		const VkShaderModule pixel_module = create_shader_module(vk_device, desc.pixel_shader);

		const VkPipelineShaderStageCreateInfo stages[] = {
			{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_module, .pName = "main" },
			{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = pixel_module, .pName = "main" },
		};

		// The vertex layout: dxc numbers the vertex shader inputs in the
		// order they are declared, so attribute i of the layout is location i.
		// Per-vertex attributes read binding 0, per-instance ones binding 1
		// (the input slots of the DirectX 12 backend).
		const VkVertexInputBindingDescription vertex_bindings[] = {
			{ .binding = 0, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
			{ .binding = 1, .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE },
		};

		std::vector<VkVertexInputAttributeDescription> attributes;
		attributes.reserve(desc.vertex_layout.size());
		bool has_instance_data = false;
		for (const vertex_attribute& attribute : desc.vertex_layout)
		{
			const bool per_instance = attribute.rate == vertex_rate::per_instance;
			has_instance_data = has_instance_data || per_instance;
			attributes.push_back({ static_cast<uint32_t>(attributes.size()), per_instance ? 1u : 0u, to_vk_format(attribute.format), attribute.offset });
		}

		const VkPipelineVertexInputStateCreateInfo vertex_input = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = has_instance_data ? 2u : 1u,
			.pVertexBindingDescriptions = vertex_bindings,
			.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
			.pVertexAttributeDescriptions = attributes.data(),
		};

		const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = desc.primitive_topology == topology::lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		};

		const VkPipelineViewportStateCreateInfo viewport_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		};

		// Front faces wind counter clockwise as seen on screen (the glTF
		// convention, and FrontCounterClockwise in the DirectX 12 backend).
		// The shaders are compiled with -fvk-invert-y, which puts Vulkan's
		// image the same way up as DirectX's, and with it the winding: the
		// same setting means the same faces in both backends.
		const VkPipelineRasterizationStateCreateInfo rasterizer = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.polygonMode = desc.fill == fill_mode::wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			.cullMode = static_cast<VkCullModeFlags>(desc.culling == cull_mode::back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE),
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.lineWidth = 1.0f,
		};

		const VkPipelineMultisampleStateCreateInfo multisampling = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		};

		// With depth testing, a pixel is only drawn when nothing nearer was
		// drawn there before; 2D drawing goes without, in drawing order.
		const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = desc.depth ? VK_TRUE : VK_FALSE,
			.depthWriteEnable = desc.depth ? VK_TRUE : VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		};

		// Standard alpha blending, so drawing with transparency works
		const VkPipelineColorBlendAttachmentState blend = {
			.blendEnable = desc.blending ? VK_TRUE : VK_FALSE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.alphaBlendOp = VK_BLEND_OP_ADD,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		};

		const VkPipelineColorBlendStateCreateInfo color_blend = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &blend,
		};

		// Set while recording instead of baked in: the viewport and scissor
		// (like DirectX 12, which always sets them on the command list) and
		// the vertex stride (which comes with the vertex buffer there too).
		const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE };
		const VkPipelineDynamicStateCreateInfo dynamic_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = static_cast<uint32_t>(std::size(dynamic_states)),
			.pDynamicStates = dynamic_states,
		};

		// Dynamic rendering: the pipeline only needs to know the format of
		// the image it draws into, no render pass object.
		const VkFormat color_format = m_swap_chain.get_format();
		const VkPipelineRenderingCreateInfo rendering_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &color_format,
			.depthAttachmentFormat = k_depth_format,
		};

		const VkGraphicsPipelineCreateInfo pipeline_info = {
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.pNext = &rendering_info,
			.stageCount = 2,
			.pStages = stages,
			.pVertexInputState = &vertex_input,
			.pInputAssemblyState = &input_assembly,
			.pViewportState = &viewport_state,
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling,
			.pDepthStencilState = &depth_stencil,
			.pColorBlendState = &color_blend,
			.pDynamicState = &dynamic_state,
			.layout = layout,
		};

		const VkPipeline vk_pipeline = m_pso_cache.get_or_create_graphics_pipeline(desc.name, pipeline_info);

		// The pipeline holds the compiled code now; the modules can go
		vkDestroyShaderModule(vk_device, vertex_module, nullptr);
		vkDestroyShaderModule(vk_device, pixel_module, nullptr);

		if (vk_pipeline == VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(vk_device, layout, nullptr);
			return nullptr;
		}

		return std::make_unique<pipeline>(std::make_unique<pipeline::native>(*this, layout, vk_pipeline, samplers.set));
	}

	//--------------------------------------------------------------
	command_list& device::native::begin_frame(color clear_color)
	{
		frame& current = m_frames[m_frame_index];

		// Wait until the gpu finished the frame that last used this frame's
		// objects, then free what was only waiting for the gpu to get there.
		m_queue.wait_for_fence_value(current.fence_value);
		m_lifetime.release_completed(m_queue.get_completed_value());
		current.scratch.reset();

		m_image_index = m_swap_chain.acquire_next_image(current.image_acquired);
		const VkImage back_buffer = m_swap_chain.get_image(m_image_index);

		// Restart recording into this frame's command pool
		vkResetCommandPool(m_device.get_vk_device(), current.command_pool, 0);
		VkCommandBuffer commands = current.command_buffer;

		const VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		vkBeginCommandBuffer(commands, &begin_info);

		// The image comes from the presentation engine and becomes our
		// render target. Its old contents are cleared anyway, so its old
		// layout does not matter (undefined).
		vk::record_barrier(commands, vk::make_image_barrier(back_buffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT));

		// The depth buffer is cleared every frame, so its old contents do not
		// matter either; the barrier waits for the previous frame's depth tests.
		const VkPipelineStageFlags2 depth_test_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		const VkAccessFlags2 depth_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		VkImageMemoryBarrier2 depth_barrier = vk::make_image_barrier(m_depth_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, depth_test_stages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, depth_test_stages, depth_access);
		depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		vk::record_barrier(commands, depth_barrier);

		const VkRenderingAttachmentInfo depth_attachment = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = m_depth_view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.clearValue = { .depthStencil = { .depth = 1.0f } },
		};

		const VkRenderingAttachmentInfo color_attachment = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = m_swap_chain.get_view(m_image_index),
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = { .color = { { clear_color.r, clear_color.g, clear_color.b, clear_color.a } } },
		};

		const VkRenderingInfo rendering_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = { .extent = { m_swap_chain.get_width(), m_swap_chain.get_height() } },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &color_attachment,
			.pDepthAttachment = &depth_attachment,
		};
		vkCmdBeginRendering(commands, &rendering_info);

		const VkViewport viewport = {
			.width = static_cast<float>(m_swap_chain.get_width()),
			.height = static_cast<float>(m_swap_chain.get_height()),
			.maxDepth = 1.0f,
		};
		vkCmdSetViewport(commands, 0, 1, &viewport);

		const VkRect2D scissor = { .extent = { m_swap_chain.get_width(), m_swap_chain.get_height() } };
		vkCmdSetScissor(commands, 0, 1, &scissor);

		m_command_list.get_native().begin(*this, commands);
		return m_command_list;
	}

	//--------------------------------------------------------------
	void device::native::end_frame()
	{
		frame& current = m_frames[m_frame_index];
		VkCommandBuffer commands = current.command_buffer;

		vkCmdEndRendering(commands);

		const VkImage back_buffer = m_swap_chain.get_image(m_image_index);
		VkImageLayout back_buffer_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkPipelineStageFlags2 last_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkAccessFlags2 last_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

		// A requested capture copies the finished image into cpu readable
		// memory first; read_capture picks it up once the gpu is done.
		if (m_capture_requested)
		{
			vk::record_barrier(commands, vk::make_image_barrier(back_buffer, back_buffer_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, last_stage, last_access, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT));
			back_buffer_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			last_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
			last_access = VK_ACCESS_2_NONE;

			const VkBufferCreateInfo capture_info = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = static_cast<VkDeviceSize>(m_swap_chain.get_width()) * m_swap_chain.get_height() * 4,
				.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			};

			const VmaAllocationCreateInfo capture_allocation_info = {
				.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.usage = VMA_MEMORY_USAGE_AUTO,
			};

			VmaAllocationInfo capture_mapping = {};
			if (vk::verify(vmaCreateBuffer(m_device.get_allocator(), &capture_info, &capture_allocation_info, &m_capture_buffer, &m_capture_allocation, &capture_mapping), "vmaCreateBuffer (capture)"))
			{
				m_capture_data = static_cast<std::byte*>(capture_mapping.pMappedData);

				const VkBufferImageCopy region = {
					.imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
					.imageExtent = { m_swap_chain.get_width(), m_swap_chain.get_height(), 1 },
				};
				vkCmdCopyImageToBuffer(commands, back_buffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_capture_buffer, 1, &region);
			}

			m_capture_requested = false;
		}

		// The image is done being a render target and can be presented
		vk::record_barrier(commands, vk::make_image_barrier(back_buffer, back_buffer_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, last_stage, last_access, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE));

		vkEndCommandBuffer(commands);
		m_last_frame_stats = m_command_list.get_native().get_stats();

		// Remember when the gpu is done with this frame, so the next time
		// this frame's objects come around begin_frame knows what to wait
		// for. Unlike DirectX, the submit waits for the image explicitly and
		// tells present when it is done drawing.
		VkSemaphore rendering_done = m_rendering_done[m_image_index];
		current.fence_value = m_queue.submit(commands, current.image_acquired, rendering_done);
		if (m_capture_buffer != VK_NULL_HANDLE && m_capture_fence_value == 0)
		{
			m_capture_fence_value = current.fence_value;
		}

		m_swap_chain.present(m_queue.get_vk_queue(), rendering_done);

		m_frame_index = (m_frame_index + 1) % k_frames_in_flight;
	}

	//--------------------------------------------------------------
	std::optional<device::native::scratch_allocation> device::native::allocate_scratch(uint32_t size, uint32_t alignment)
	{
		frame& current = m_frames[m_frame_index];

		const std::optional<uint32_t> offset = current.scratch.allocate(size, alignment);
		if (!offset)
		{
			return std::nullopt;
		}

		return scratch_allocation{ current.scratch_data + *offset, current.scratch_buffer, *offset };
	}

	//--------------------------------------------------------------
	VkDescriptorSet device::native::get_data_set(data_slot slot) const
	{
		return m_frames[m_frame_index].data_sets[static_cast<uint32_t>(slot)];
	}

	//--------------------------------------------------------------
	std::vector<uint8_t> device::native::read_capture()
	{
		if (m_capture_buffer == VK_NULL_HANDLE)
		{
			return {};
		}

		m_queue.wait_for_fence_value(m_capture_fence_value);
		vmaInvalidateAllocation(m_device.get_allocator(), m_capture_allocation, 0, VK_WHOLE_SIZE);

		const size_t size = static_cast<size_t>(m_swap_chain.get_width()) * m_swap_chain.get_height() * 4;
		std::vector<uint8_t> pixels(size);
		std::memcpy(pixels.data(), m_capture_data, size);

		// The swap chain may store blue first (B8G8R8A8); captures are rgba
		if (m_swap_chain.get_format() == VK_FORMAT_B8G8R8A8_UNORM)
		{
			for (size_t pixel = 0; pixel < size; pixel += 4)
			{
				std::swap(pixels[pixel], pixels[pixel + 2]);
			}
		}

		vmaDestroyBuffer(m_device.get_allocator(), m_capture_buffer, m_capture_allocation);
		m_capture_buffer = VK_NULL_HANDLE;
		m_capture_allocation = VK_NULL_HANDLE;
		m_capture_data = nullptr;
		m_capture_fence_value = 0;
		return pixels;
	}

	//--------------------------------------------------------------
	void device::native::release_later(std::function<void()> release)
	{
		// Anything recorded so far is done once the gpu reaches the fence
		// value of the next submit.
		m_lifetime.release_later(m_queue.get_next_fence_value(), std::move(release));
	}

	//--------------------------------------------------------------
	void device::native::shutdown()
	{
		if (m_is_shut_down)
		{
			return;
		}

		// Make sure the gpu is idle, then free everything that waited for it
		m_queue.flush();
		m_lifetime.release_all();
		m_lifetime.check_all_destroyed("Vulkan");

		// Store the pipelines built this run, so the next run starts faster
		m_pso_cache.save();

		m_is_shut_down = true;
	}

	//--------------------------------------------------------------
	frame_stats device::native::get_frame_stats() const
	{
		return m_last_frame_stats;
	}

	//--------------------------------------------------------------
	void device::native::request_capture()
	{
		m_capture_requested = true;
	}

	//--------------------------------------------------------------
	uint32_t device::native::get_width() const
	{
		return m_swap_chain.get_width();
	}

	//--------------------------------------------------------------
	uint32_t device::native::get_height() const
	{
		return m_swap_chain.get_height();
	}

	//--------------------------------------------------------------
	void device::native::resource_created()
	{
		m_lifetime.resource_created();
	}

	//--------------------------------------------------------------
	vulkan::descriptor_heap& device::native::get_texture_heap()
	{
		return m_texture_heap;
	}

	//--------------------------------------------------------------
	VkDevice device::native::get_vk_device() const
	{
		return m_device.get_vk_device();
	}

	//--------------------------------------------------------------
	VmaAllocator device::native::get_allocator() const
	{
		return m_device.get_allocator();
	}
}
