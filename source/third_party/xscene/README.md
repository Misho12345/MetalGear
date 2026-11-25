# xscene

Reads scene files (`.scene.json`) into plain data: a named list of nodes, each placing a model (`.glb`/`.gltf`) or another scene with a position, rotation and scale.

It only reads the scene structure. It doesn't parse json text, do matrix math or load files. Loading the models and scenes the nodes name, and drawing them, is up to your game.

## Install

1. Unzip so the folder sits at `source/third_party/xscene/` (this file is then `source/third_party/xscene/README.md`).
2. In `source/third_party/CMakeLists.txt`, add this line at the end (skip this step if the file already mentions `xscene`):

   ```cmake
   ADD_SUBDIRECTORY(${SOURCE_THIRDPARTY_DIRECTORY}/xscene)
   ```

3. Add `xscene` to the `LINKS` of your game in `source/games/<your_game>/CMakeLists.txt`:

   ```cmake
   BUZ_GAME(my_game
            LINKS buz_engine buz_game_framework buz_renderer_3d buz_asset_management xscene)
   ```

4. Run `generate.bat` again.

## Use

xscene depends on nothing but the standard library, so your game connects two things.

**Json.** xscene doesn't parse json text. Your game parses the file with its own json library and hands xscene the root value through a class derived from `xscene::json_value`. The engine's `buz::json` is nlohmann::json, and the `json_view` class below connects it.

**Math.** A node's transform comes back as plain numbers (`translation`, `rotation` as a quaternion `[x, y, z, w]`, `scale`). `make_matrix` below turns them into a `buz::mat4`.

These are the two files this engine's own game uses; copy them into `source/games/<your_game>/private/` and change what you need. `scene.h` declares the game's own scene types:

```cpp
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
```

`scene.cpp` connects xscene and fills those types:

```cpp
#include "scene.h"

#include "core/assert.h"
#include "core/log.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <xscene.h>

#include <cstdint>
#include <memory>
#include <string>

namespace
{
    // A read-only view of a buz::json value, in the form xscene reads json.
    // Like std::string_view, it refers to json it does not own: the json
    // tree must outlive the view. parse_scene keeps the root alive for the
    // whole parse.
    class json_view final : public xscene::json_value
    {
    public:
        explicit json_view(const buz::json& value);

    protected:
        xscene::json_kind on_get_kind() const override;
        std::string on_get_string() const override;
        double on_get_number() const override;
        int64_t on_get_integer() const override;
        uint32_t on_get_size() const override;
        std::unique_ptr<xscene::json_value> on_get_member(std::string_view key) const override;
        std::unique_ptr<xscene::json_value> on_get_element(uint32_t index) const override;

    private:
        const buz::json& m_value; // the value this view reads; part of the caller's json tree
    };

    //-------------------------------------------------------------------------
    json_view::json_view(const buz::json& value)
        : m_value(value)
    {
    }

    //-------------------------------------------------------------------------
    xscene::json_kind json_view::on_get_kind() const
    {
        switch (m_value.type())
        {
        case buz::json::value_t::boolean:
            return xscene::json_kind::boolean;
        case buz::json::value_t::number_integer:
        case buz::json::value_t::number_unsigned:
            return xscene::json_kind::integer;
        case buz::json::value_t::number_float:
            return xscene::json_kind::number;
        case buz::json::value_t::string:
            return xscene::json_kind::string;
        case buz::json::value_t::array:
            return xscene::json_kind::array;
        case buz::json::value_t::object:
            return xscene::json_kind::object;
        default:
            // null, binary, and discarded (a file that did not parse): none
            // of them is a value xscene reads
            return xscene::json_kind::null;
        }
    }

    //-------------------------------------------------------------------------
    std::string json_view::on_get_string() const
    {
        return m_value.get<std::string>();
    }

    //-------------------------------------------------------------------------
    double json_view::on_get_number() const
    {
        return m_value.get<double>();
    }

    //-------------------------------------------------------------------------
    int64_t json_view::on_get_integer() const
    {
        return m_value.get<int64_t>();
    }

    //-------------------------------------------------------------------------
    uint32_t json_view::on_get_size() const
    {
        return static_cast<uint32_t>(m_value.size());
    }

    //-------------------------------------------------------------------------
    std::unique_ptr<xscene::json_value> json_view::on_get_member(std::string_view key) const
    {
        const auto found = m_value.find(std::string(key));
        if (found == m_value.end())
        {
            return nullptr;
        }

        return std::make_unique<json_view>(*found);
    }

    //-------------------------------------------------------------------------
    std::unique_ptr<xscene::json_value> json_view::on_get_element(uint32_t index) const
    {
        BUZ_ASSERT_MSG(index < m_value.size(), "xscene asked for element {} of an array of {}", index, m_value.size());
        return std::make_unique<json_view>(m_value[index]);
    }

    //-------------------------------------------------------------------------
    buz::mat4 make_matrix(const xscene::transform& placement)
    {
        const buz::vec3 position = { placement.translation[0], placement.translation[1], placement.translation[2] };
        // glm takes w first; the file writes it last
        const buz::quat orientation = buz::quat(placement.rotation[3], placement.rotation[0], placement.rotation[1], placement.rotation[2]);
        const buz::vec3 size = { placement.scale[0], placement.scale[1], placement.scale[2] };

        const buz::mat4 translation = glm::translate(buz::mat4(1.0F), position);
        const buz::mat4 rotation = glm::mat4_cast(orientation);
        const buz::mat4 scale = glm::scale(buz::mat4(1.0F), size);

        return translation * rotation * scale;
    }

    //-------------------------------------------------------------------------
    scene_node make_node(const xscene::node& source)
    {
        const scene_node_kind kind = source.kind == xscene::node_kind::model ? scene_node_kind::model : scene_node_kind::scene;
        return { source.name, kind, source.asset_id, make_matrix(source.local_transform) };
    }
}

//-------------------------------------------------------------------------
std::optional<scene> parse_scene(const buz::json& data, std::string_view scene_id)
{
    const xscene::parse_result result = xscene::parse(json_view(data), scene_id);
    if (!result.parsed)
    {
        buz::log::error("{}", result.error);
        return std::nullopt;
    }

    scene converted;
    converted.name = result.parsed->name;
    converted.nodes.reserve(result.parsed->nodes.size());
    for (const xscene::node& source : result.parsed->nodes)
    {
        converted.nodes.push_back(make_node(source));
    }

    return converted;
}
```

Then load a scene and walk its nodes:

```cpp
const buz::json data = buz::load_json("my_game/dock.scene.json");
const std::optional<scene> dock = parse_scene(data, "my_game/dock.scene.json");
if (dock)
{
    for (const scene_node& node : dock->nodes)
    {
        // node.kind says whether node.asset_id names a model or a scene;
        // node.local_transform places it inside the scene
    }
}
```

`asset_id` is already a content-relative path (the path `buz::load_model` and `buz::load_json` take), so you can pass it straight to the engine's loaders.

A scene can contain other scenes. To get a node's world transform, multiply down the chain: `parent_world_transform * node.local_transform`. Guard against a scene that contains itself, or your game recurses until it crashes.

The coding standards ask game code to see engine names only. Keeping xscene inside `scene.cpp`, as above, does that: the rest of your game includes `scene.h` and never names xscene.

## The file format

```json
{
    "format": "mgs-scene",
    "version": 1,
    "name": "dock",
    "nodes": [
        { "name": "crate", "mesh": "meshes/crate.glb", "translation": [0, 0, -4] },
        { "name": "boxes", "scene": "scenes/boxes.scene.json", "rotation": [0, 0.7071, 0, 0.7071], "scale": [2, 2, 2] }
    ]
}
```

| Key | Required | Meaning |
|---|---|---|
| `format` | yes | always `"mgs-scene"` |
| `version` | yes | always `1` |
| `name` | yes | the scene's name |
| `nodes` | yes | the list of nodes, can be empty |
| node `name` | yes | used in error messages |
| node `mesh` or `scene` | exactly one | path to a model or a scene file, relative to this scene file |
| node `translation` | no | `[x, y, z]`, default `[0, 0, 0]` |
| node `rotation` | no | quaternion `[x, y, z, w]` (glTF order), default `[0, 0, 0, 1]` |
| node `scale` | no | `[x, y, z]`, default `[1, 1, 1]` |

One malformed node rejects the whole file, and the error names the file and the node.

## Needs

C++20 and the standard library, nothing else. You connect your own json library through `xscene::json_value`, and your own math library for the matrices (see Use).
