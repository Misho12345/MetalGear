#include "metal_gear_game.h"

#include "engine/engine.h"

int main(const int argc, char** argv)
{
	buz::engine engine(argc, argv, { .window_title = "Metal Gear" });

	metal_gear_game game(engine);
	return engine.run(game);
}
