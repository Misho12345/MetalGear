#pragma once

#include "asset_cache.h"
#include "level.h"

#include "game_framework/game_3d.h"
#include "renderer_2d/font.h"

class metal_gear_game final : public buz::game_3d
{
public:
	explicit metal_gear_game(buz::engine& engine);

protected:
	void on_render_world(buz::engine& engine, buz::world_renderer_3d& world) override;
	void on_render_screen(buz::engine& engine, buz::screen_renderer& screen) override;

private:
	buz::font m_font;
	asset_cache m_cache; // declared before m_level: the level points into it, so it must be destroyed after
	level m_level;
};
