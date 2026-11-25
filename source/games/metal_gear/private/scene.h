#pragma once

#include "core/json.h"
#include "core/math.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A scene: a named list of nodes, each adding a model or another scene, as
// written in a .scene.json file ("format": "mgs-scene"). The xscene library
// (source/third_party/xscene) reads the file; scene.cpp is the game's one
// wrapper around it and turns what it reads into the types below.
//
//     const buz::json data = buz::load_json("metal_gear_solid/dock/dock.scene.json");
//     std::optional<scene> dock = parse_scene(data, "metal_gear_solid/dock/dock.scene.json");
//
// Ownership: a scene is plain data. Its nodes refer to assets by id (the
// content-relative path) instead of by pointer, so a scene has no pointers
// that can dangle. The asset_registry owns every scene and every asset a
// scene names.
// Single threaded.
// Failure: parse_scene logs what is wrong with the file and returns nothing.

//-------------------------------------------------------------------------
// Whether a scene node adds a model or a scene.
enum class scene_node_kind : uint8_t
{
    model,
    scene
};

//-------------------------------------------------------------------------
// One entry of a scene: a model or a scene, positioned relative to the scene
// that contains the node.
struct scene_node
{
    std::string name;                              // as named in the file, for messages
    scene_node_kind kind = scene_node_kind::model; // whether asset_id names a model or a scene
    std::string asset_id;                          // content-relative path of the model or scene, already resolved against the scene file
    buz::mat4 local_transform = buz::mat4(1.0F);   // node space to the space of the scene the node is in
};

//-------------------------------------------------------------------------
// A parsed scene file.
struct scene
{
    std::string name;              // the file's "name"; unique among the loaded scenes
    std::vector<scene_node> nodes; // in file order
};

// Parses a scene file's json. scene_id is the file's content-relative path:
// the paths inside the file are relative to it.
[[nodiscard]] std::optional<scene> parse_scene(const buz::json& data, std::string_view scene_id);