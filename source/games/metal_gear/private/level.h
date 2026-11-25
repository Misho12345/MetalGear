#pragma once
#include "core/math.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class asset_cache;
struct loaded_model;

namespace buz
{
	class world_renderer_3d;
}

// One model standing somewhere in the level
struct placement
{
	std::string name;          // node names from the root scene down, e.g. "dock_main/crates/container_a_12"
	const loaded_model* model; // owned by the asset_cache
	buz::mat4 world;           // model space to world space
};

class level final
{
public:
	level(asset_cache& cache, const std::string& scene_id, const buz::mat4& root);

	void draw(buz::world_renderer_3d& world) const;

	[[nodiscard]] const placement* find(std::string_view name) const;
	[[nodiscard]] std::size_t get_placement_count() const;

private:
	enum class pass
	{
		opaque,
		see_through
	};

	void add_scene(asset_cache& cache, const std::string& scene_id, const buz::mat4& parent_world, const std::string& name_prefix, std::vector<std::string>& visiting);
	void draw_pass(buz::world_renderer_3d& world, pass which) const;

	std::vector<placement> placements_;
};
