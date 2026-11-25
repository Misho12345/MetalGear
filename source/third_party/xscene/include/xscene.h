// xscene.h - reads scene files (.scene.json) into plain data
//
// A scene is a named list of nodes. Each node places a model (a .glb or
// .gltf file) or another scene, with a glTF-style transform:
//
//     {
//         "format": "mgs-scene",
//         "version": 1,
//         "name": "dock",
//         "nodes": [
//             { "name": "crate", "mesh": "meshes/crate.glb", "translation": [0, 0, -4] },
//             { "name": "boxes", "scene": "scenes/boxes.scene.json", "rotation": [0, 0.7071, 0, 0.7071], "scale": [2, 2, 2] }
//         ]
//     }
//
// "translation", "rotation" ([x, y, z, w]) and "scale" are optional; a
// missing one is the identity. Paths inside a file are relative to that file.
//
// xscene depends on nothing but the standard library. It does not parse json
// text: the game parses the file with its own json library and hands xscene
// the root value through a class it derives from xscene::json_value. It does
// not do math either: a node's transform comes back as plain numbers, for the
// game to turn into a matrix with its own math library.
//
//     const buz::json data = buz::load_json("my_game/dock.scene.json");
//     xscene::parse_result result = xscene::parse(json_view(data), "my_game/dock.scene.json");
//     if (!result.parsed)
//     {
//         buz::log::error("{}", result.error);
//     }
//
// Ownership: a scene is plain data. Nodes name their model or scene by id
// (its content-relative path), never by pointer; loading what an id names
// is up to the caller.
// Threading: parse keeps no state, so separate calls may run on separate
// threads, as far as the json_value implementation allows.
// Failure: one malformed node rejects the whole file. parse then returns no
// scene and an error message that names the file and the node. It never
// throws and never logs.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xscene
{
    // The "format" and "version" a scene file must declare
    inline constexpr std::string_view k_format = "mgs-scene";
    inline constexpr int32_t k_supported_version = 1;

    //-------------------------------------------------------------------------
    // What a json value holds. A number with a fraction or exponent is
    // number; a whole number is integer.
    enum class json_kind : uint8_t
    {
        null,
        boolean,
        integer,
        number,
        string,
        array,
        object
    };

    //-------------------------------------------------------------------------
    // One json value, as xscene reads it. Derive from it to connect your json
    // library (see the README for one for buz::json). The public functions
    // are fixed; each calls the protected virtual of the same name with on_
    // in front, which is what a derived class overrides.
    //
    // xscene asks for the kind first and only then calls the one accessor
    // that fits that kind, so an implementation does not need to handle a
    // mismatch (an assert is enough). A value from get_member or get_element
    // may refer into its parent's json, so it must not outlive the root.
    class json_value
    {
    public:
        virtual ~json_value() = default;

        // What the value holds
        json_kind get_kind() const { return on_get_kind(); }

        // The text of a string value
        std::string get_string() const { return on_get_string(); }

        // The value of an integer or number value
        double get_number() const { return on_get_number(); }

        // The value of an integer value
        int64_t get_integer() const { return on_get_integer(); }

        // The number of elements of an array value
        uint32_t get_size() const { return on_get_size(); }

        // The member with this key of an object value, or nullptr when the
        // object has no such member
        std::unique_ptr<json_value> get_member(std::string_view key) const { return on_get_member(key); }

        // The element at this index (below get_size()) of an array value
        std::unique_ptr<json_value> get_element(uint32_t index) const { return on_get_element(index); }

    protected:
        virtual json_kind on_get_kind() const = 0;
        virtual std::string on_get_string() const = 0;
        virtual double on_get_number() const = 0;
        virtual int64_t on_get_integer() const = 0;
        virtual uint32_t on_get_size() const = 0;
        virtual std::unique_ptr<json_value> on_get_member(std::string_view key) const = 0;
        virtual std::unique_ptr<json_value> on_get_element(uint32_t index) const = 0;
    };

    //-------------------------------------------------------------------------
    // A node's placement as the file writes it, glTF style. Apply scale
    // first, then rotation, then translation: as matrices,
    // translation * rotation * scale.
    struct transform
    {
        std::array<float, 3> translation = { 0.0F, 0.0F, 0.0F }; // x, y, z
        std::array<float, 4> rotation = { 0.0F, 0.0F, 0.0F, 1.0F }; // unit quaternion x, y, z, w (glTF order: w last)
        std::array<float, 3> scale = { 1.0F, 1.0F, 1.0F };         // x, y, z
    };

    //-------------------------------------------------------------------------
    // Whether a node places a model or a scene
    enum class node_kind : uint8_t
    {
        model,
        scene
    };

    //-------------------------------------------------------------------------
    // One entry of a scene: a model or a scene, positioned relative to the
    // scene that contains the node
    struct node
    {
        std::string name;                  // as named in the file, for messages
        node_kind kind = node_kind::model; // whether asset_id names a model or a scene
        std::string asset_id;              // content-relative path of the model or scene, already resolved against the scene file
        transform local_transform;         // node space to the space of the scene the node is in
    };

    //-------------------------------------------------------------------------
    // A parsed scene file
    struct scene
    {
        std::string name;        // the file's "name"
        std::vector<node> nodes; // in file order
    };

    //-------------------------------------------------------------------------
    // What parse returns: a scene, or the reason there is none
    struct parse_result
    {
        std::optional<scene> parsed; // nothing when the file is malformed
        std::string error;           // why parsed is empty; empty when it is not
    };

    // Reads a scene file's root json value. scene_id is the file's
    // content-relative path, with / between folders: the paths inside the
    // file are relative to it.
    [[nodiscard]] parse_result parse(const json_value& root, std::string_view scene_id);
}
