#include "vulkan/backend/command_list.h"

#include "core/assert.h"
#include "vulkan/backend/buffer.h"
#include "vulkan/backend/device.h"
#include "vulkan/backend/gpu_texture.h"
#include "vulkan/backend/pipeline.h"
#include "vulkan/descriptor_heap.h"

#include <cstring>
#include <optional>

namespace buz::rhi
{
	// rhi::command_list hands every call to its native object below. Only
	// this backend's files are compiled (see rhi/CMakeLists.txt).

	//--------------------------------------------------------------
	command_list::command_list(std::unique_ptr<native> data)
		: m_native(std::move(data))
		, m_pipeline(nullptr)
	{
	}

	//--------------------------------------------------------------
	command_list::~command_list() = default;

	//--------------------------------------------------------------
	void command_list::set_pipeline(const pipeline& active_pipeline)
	{
		m_native->set_pipeline(active_pipeline.get_native());
		m_pipeline = &active_pipeline;
	}

	//--------------------------------------------------------------
	void command_list::set_vertex_buffer(const buffer& vertices)
	{
		m_native->set_vertex_buffer(vertices.get_native());
	}

	//--------------------------------------------------------------
	bool command_list::set_vertex_data(vertex_rate rate, std::span<const std::byte> data, uint32_t stride)
	{
		return m_native->set_vertex_data(rate, data, stride);
	}

	//--------------------------------------------------------------
	void command_list::set_index_buffer(const buffer& indices)
	{
		m_native->set_index_buffer(indices.get_native());
	}

	//--------------------------------------------------------------
	void command_list::set_constants(std::span<const std::byte> constants)
	{
		m_native->set_constants(constants);
	}

	//--------------------------------------------------------------
	void command_list::set_texture(const gpu_texture& sampled_texture)
	{
		m_native->set_texture(sampled_texture.get_native());
	}

	//--------------------------------------------------------------
	void command_list::draw(uint32_t vertex_count, uint32_t instance_count)
	{
		m_native->draw(vertex_count, instance_count);
	}

	//--------------------------------------------------------------
	void command_list::draw_indexed(uint32_t index_count, uint32_t instance_count)
	{
		m_native->draw_indexed(index_count, instance_count);
	}

	//--------------------------------------------------------------
	const pipeline* command_list::get_pipeline() const
	{
		return m_pipeline;
	}

	//--------------------------------------------------------------
	command_list::native& command_list::get_native()
	{
		return *m_native;
	}

	//--------------------------------------------------------------
	command_list::native::native()
		: m_owner(nullptr)
		, m_command_buffer(VK_NULL_HANDLE)
		, m_layout(VK_NULL_HANDLE)
	{
	}

	//--------------------------------------------------------------
	void command_list::native::begin(device::native& owner, VkCommandBuffer command_buffer)
	{
		m_owner = &owner;
		m_command_buffer = command_buffer;
		m_layout = VK_NULL_HANDLE;
		m_stats = {};
	}

	//--------------------------------------------------------------
	void command_list::native::set_pipeline(const pipeline::native& active_pipeline)
	{
		m_layout = active_pipeline.get_layout();

		vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, active_pipeline.get_vk_pipeline());

		// Set 1: the pipeline's sampler (a static sampler in DirectX 12)
		const VkDescriptorSet sampler_set = active_pipeline.get_sampler_set();
		vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 1, 1, &sampler_set, 0, nullptr);
	}

	//--------------------------------------------------------------
	void command_list::native::set_vertex_buffer(const buffer::native& vertices)
	{
		BUZ_ASSERT(vertices.get_usage() == buffer_usage::vertex);

		// The stride comes with the buffer, like a DirectX 12 vertex buffer
		// view (the pipeline leaves it dynamic).
		const VkBuffer handle = vertices.get_vk_buffer();
		const VkDeviceSize offset = 0;
		const VkDeviceSize stride = vertices.get_stride();
		vkCmdBindVertexBuffers2(m_command_buffer, 0, 1, &handle, &offset, nullptr, &stride);
	}

	//--------------------------------------------------------------
	bool command_list::native::set_vertex_data(vertex_rate rate, std::span<const std::byte> data, uint32_t stride)
	{
		const std::optional<device::native::scratch_allocation> scratch = m_owner->allocate_scratch(static_cast<uint32_t>(data.size()));
		if (!scratch)
		{
			return false;
		}

		std::memcpy(scratch->data, data.data(), data.size());

		// Stream 0 holds per-vertex data, stream 1 per-instance data
		const VkDeviceSize vk_stride = stride;
		vkCmdBindVertexBuffers2(m_command_buffer, rate == vertex_rate::per_instance ? 1 : 0, 1, &scratch->buffer, &scratch->offset, nullptr, &vk_stride);
		return true;
	}

	//--------------------------------------------------------------
	void command_list::native::set_index_buffer(const buffer::native& indices)
	{
		BUZ_ASSERT(indices.get_usage() == buffer_usage::index16 || indices.get_usage() == buffer_usage::index32);
		vkCmdBindIndexBuffer(m_command_buffer, indices.get_vk_buffer(), 0, indices.get_usage() == buffer_usage::index32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
	}

	//--------------------------------------------------------------
	void command_list::native::set_constants(std::span<const std::byte> constants)
	{
		// Push constants: Vulkan's root constants
		vkCmdPushConstants(m_command_buffer, m_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(constants.size()), constants.data());
	}

	//--------------------------------------------------------------
	void command_list::native::set_texture(const gpu_texture::native& sampled_texture)
	{
		// Set 0: the texture's descriptor set, from the texture heap
		const VkDescriptorSet texture_set = m_owner->get_texture_heap().get_set(sampled_texture.get_descriptor_index());
		vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &texture_set, 0, nullptr);
	}

	//--------------------------------------------------------------
	void command_list::native::draw(uint32_t vertex_count, uint32_t instance_count)
	{
		vkCmdDraw(m_command_buffer, vertex_count, instance_count, 0, 0);
		m_stats.draw_calls += 1;
		m_stats.instances += instance_count;
	}

	//--------------------------------------------------------------
	void command_list::native::draw_indexed(uint32_t index_count, uint32_t instance_count)
	{
		vkCmdDrawIndexed(m_command_buffer, index_count, instance_count, 0, 0, 0);
		m_stats.draw_calls += 1;
		m_stats.instances += instance_count;
	}

	//--------------------------------------------------------------
	const frame_stats& command_list::native::get_stats() const
	{
		return m_stats;
	}
}
