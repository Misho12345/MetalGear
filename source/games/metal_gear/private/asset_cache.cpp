#include "asset_cache.h"

#include "scene.h"
#include "asset_management/model_loading.h"
#include "asset_management/texture_loading.h"
#include "core/assert.h"
#include "core/filesystem.h"
#include "core/json.h"

namespace
{
	bool is_normalised(const std::string& asset_id)
	{
		return buz::fs::path(asset_id).lexically_normal().generic_string() == asset_id;
	}

	buz::mat4 to_matrix(const buz::node_transform& transform)
	{
		const buz::mat4 translation = glm::translate(buz::mat4(1.0f), transform.translation);
		const buz::mat4 rotation = glm::mat4_cast(transform.rotation);
		const buz::mat4 scale = glm::scale(buz::mat4(1.0f), transform.scale);
		return translation * rotation * scale;
	}
}

const loaded_model* asset_cache::get_model(const std::string& asset_id)
{
	BUZ_ASSERT_MSG(is_normalised(asset_id), "Asset id \"{}\" is not normalised", asset_id);

	auto [it, inserted] = models_.try_emplace(asset_id);
	if (inserted)
	{
		if (std::optional<buz::model> source = buz::load_model(engine_, asset_id))
		{
			it->second = make_loaded_model(std::move(*source));
		}
	}

	return it->second ? &*it->second : nullptr;
}

const scene* asset_cache::get_scene(const std::string& asset_id)
{
	BUZ_ASSERT_MSG(is_normalised(asset_id), "Asset id \"{}\" is not normalised", asset_id);

	auto [it, inserted] = scenes_.try_emplace(asset_id);
	if (inserted)
	{
		const buz::json data = buz::load_json(asset_id);
		it->second = parse_scene(data, asset_id);
	}

	return it->second ? &*it->second : nullptr;
}

loaded_model asset_cache::make_loaded_model(buz::model source)
{
	loaded_model result;
	result.model = std::move(source);

	const std::vector<buz::model_node>& nodes = result.model.nodes;
	result.textures.reserve(nodes.size());
	result.node_world.reserve(nodes.size());

	for (const buz::model_node& node : nodes)
	{
		result.textures.push_back(node.look.base_texture.has_image() ? get_texture(node.look.base_texture) : nullptr);

		const buz::mat4 local = to_matrix(node.local_transform);
		if (node.parent < 0)
		{
			result.node_world.push_back(local);
		}
		else
		{
			BUZ_ASSERT_MSG(static_cast<std::size_t>(node.parent) < result.node_world.size(), "Node \"{}\" comes before its parent", node.name);
			result.node_world.push_back(result.node_world[static_cast<std::size_t>(node.parent)] * local);
		}
	}

	return result;
}

std::shared_ptr<buz::texture> asset_cache::get_texture(const buz::texture_id& id)
{
	auto [it, inserted] = textures_.try_emplace(id);
	if (inserted)
	{
		it->second = buz::load_texture(engine_, id);
	}

	return it->second;
}
