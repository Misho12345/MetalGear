#pragma once
#include "scene.h"
#include "renderer_3d/model.h"

namespace buz
{
	class engine;
	class texture;
}

struct loaded_model final
{
	buz::model model;
	std::vector<std::shared_ptr<buz::texture>> textures;
	std::vector<buz::mat4> node_world;
};

class asset_cache final
{
public:
	asset_cache(buz::engine& engine) : engine_(engine) {}

	asset_cache(const asset_cache&) = delete;
	asset_cache(asset_cache&&) noexcept = delete;
	asset_cache& operator=(const asset_cache&) = delete;
	asset_cache& operator=(asset_cache&&) noexcept = delete;

	[[nodiscard]] const loaded_model* get_model(const std::string& asset_id);
	[[nodiscard]] const scene* get_scene(const std::string& asset_id);

private:
	[[nodiscard]] loaded_model make_loaded_model(buz::model source);
	[[nodiscard]] std::shared_ptr<buz::texture> get_texture(const buz::texture_id& id);

	buz::engine& engine_;

	std::unordered_map<std::string, std::optional<loaded_model>> models_;
	std::unordered_map<std::string, std::optional<scene>> scenes_;
	std::unordered_map<buz::texture_id, std::shared_ptr<buz::texture>> textures_;
};
