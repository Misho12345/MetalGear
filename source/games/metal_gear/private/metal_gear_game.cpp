#include "metal_gear_game.h"

#include "asset_management/texture_loading.h"
#include "engine/engine.h"
#include "engine/window.h"
#include "renderer_2d/screen_renderer.h"
#include "renderer_3d/world_renderer_3d.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

metal_gear_game::metal_gear_game(buz::engine& engine)
	: game_3d(engine)
	, m_cache(engine)
	, m_level(m_cache, "metal_gear/dock/dock.scene.json", glm::scale(buz::mat4(1.0f), buz::vec3(0.1f))) // the assets use ~10 cm per unit
{
	m_font.atlas = buz::load_texture(engine, "shared/fonts/font_8x8.png");
}

void metal_gear_game::on_render_world(buz::engine& engine, buz::world_renderer_3d& world)
{
	// Temporary overview camera, until the follow camera exists
	const buz::vec2 size = engine.get_window().get_size();
	const float aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
	const buz::mat4 view = glm::lookAtRH(buz::vec3(25.0f, 40.0f, 25.0f), buz::vec3(0.0f), buz::vec3(0.0f, 1.0f, 0.0f));
	const buz::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 200.0f);
	world.set_camera(view, projection);

	m_level.draw(world);
}

void metal_gear_game::on_render_screen(buz::engine& /*engine*/, buz::screen_renderer& screen)
{
	const buz::color text_color = { 1.0f, 1.0f, 1.0f, 0.8f };
	screen.draw_text(m_font, "F1 wireframe   F2 lighting   F3 stats   F4 debug lines", { 16.0f, 16.0f }, 12.0f, text_color);
}
