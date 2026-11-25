#include "level.h"

#include "asset_cache.h"
#include "core/log.h"
#include "render_core/texture.h"
#include "renderer_3d/mesh.h"
#include "renderer_3d/world_renderer_3d.h"

#include <algorithm>

level::level(asset_cache& cache, const std::string& scene_id, const buz::mat4& root)
{
	std::vector<std::string> visiting;
	add_scene(cache, scene_id, root, "", visiting);
	buz::log::info("Level \"{}\": {} placements", scene_id, placements_.size());
}

void level::add_scene(asset_cache& cache, const std::string& scene_id, const buz::mat4& parent_world, const std::string& name_prefix, std::vector<std::string>& visiting)
{
	// A scene that contains itself (directly or through others) would recurse forever
	if (std::ranges::find(visiting, scene_id) != visiting.end())
	{
		buz::log::error("Scene \"{}\" contains itself; skipped", scene_id);
		return;
	}

	const scene* source = cache.get_scene(scene_id);
	if (source == nullptr)
	{
		return; // the cache already logged why
	}

	visiting.push_back(scene_id);
	for (const scene_node& node : source->nodes)
	{
		const buz::mat4 world = parent_world * node.local_transform;
		const std::string name = name_prefix.empty() ? node.name : name_prefix + "/" + node.name;

		if (node.kind == scene_node_kind::scene)
		{
			add_scene(cache, node.asset_id, world, name, visiting);
		}
		else if (const loaded_model* model = cache.get_model(node.asset_id))
		{
			placements_.push_back({ name, model, world });
		}
	}
	visiting.pop_back();
}

void level::draw(buz::world_renderer_3d& world) const
{
	// See-through parts (the water) last, so whatever is behind them is already drawn
	draw_pass(world, pass::opaque);
	draw_pass(world, pass::see_through);
}

void level::draw_pass(buz::world_renderer_3d& world, pass which) const
{
	for (const placement& item : placements_)
	{
		const std::vector<buz::model_node>& nodes = item.model->model.nodes;
		for (std::size_t i = 0; i < nodes.size(); ++i)
		{
			const buz::model_node& node = nodes[i];
			const pass node_pass = node.look.base_color.a < 1.0f ? pass::see_through : pass::opaque;
			if (!node.shape || node_pass != which)
			{
				continue;
			}

			const buz::mat4 transform = item.world * item.model->node_world[i];
			if (const std::shared_ptr<buz::texture>& surface = item.model->textures[i])
			{
				world.draw(*node.shape, node.look, *surface, transform);
			}
			else
			{
				world.draw(*node.shape, node.look, transform);
			}
		}
	}
}

const placement* level::find(std::string_view name) const
{
	const auto found = std::ranges::find(placements_, name, &placement::name);
	return found != placements_.end() ? &*found : nullptr;
}

std::size_t level::get_placement_count() const
{
	return placements_.size();
}
