#include "xscene.h"

#include <filesystem>
#include <format>
#include <utility>

namespace xscene
{
    namespace
    {
        //-------------------------------------------------------------------------
        // A value, or the reason there is none
        template <typename T>
        struct read_result
        {
            std::optional<T> value; // nothing when the input is malformed
            std::string error;      // why value is empty; empty when it is not
        };

        //-------------------------------------------------------------------------
        template <typename T>
        read_result<T> fail(std::string error)
        {
            return { std::nullopt, std::move(error) };
        }

        //-------------------------------------------------------------------------
        bool is_number(json_kind kind)
        {
            return kind == json_kind::integer || kind == json_kind::number;
        }

        //-------------------------------------------------------------------------
        // The string member with this key; nothing when it is missing or not a string
        std::optional<std::string> read_string(const json_value& object, std::string_view key)
        {
            const std::unique_ptr<json_value> member = object.get_member(key);
            if (member == nullptr || member->get_kind() != json_kind::string)
            {
                return std::nullopt;
            }

            return member->get_string();
        }

        //-------------------------------------------------------------------------
        // The member with this key as Count numbers, or fallback when the
        // object has no such member; nothing when the member is not an array
        // of exactly Count numbers
        template <uint32_t Count>
            requires(Count > 0)
        std::optional<std::array<float, Count>> read_floats_or(const json_value& object, std::string_view key, const std::array<float, Count>& fallback)
        {
            const std::unique_ptr<json_value> member = object.get_member(key);
            if (member == nullptr)
            {
                return fallback;
            }
            if (member->get_kind() != json_kind::array || member->get_size() != Count)
            {
                return std::nullopt;
            }

            std::array<float, Count> numbers = {};
            for (uint32_t index = 0; index < Count; ++index)
            {
                const std::unique_ptr<json_value> element = member->get_element(index);
                if (element == nullptr || !is_number(element->get_kind()))
                {
                    return std::nullopt;
                }
                numbers[index] = static_cast<float>(element->get_number());
            }

            return numbers;
        }

        //-------------------------------------------------------------------------
        // Reads translation, rotation ([x, y, z, w]) and scale
        read_result<transform> read_transform(const json_value& data, std::string_view scene_id, std::string_view node_name)
        {
            const transform identity;

            const std::optional<std::array<float, 3>> translation = read_floats_or<3>(data, "translation", identity.translation);
            if (!translation)
            {
                return fail<transform>(std::format("Scene \"{}\", node \"{}\": translation must be 3 numbers", scene_id, node_name));
            }
            const std::optional<std::array<float, 4>> rotation = read_floats_or<4>(data, "rotation", identity.rotation);
            if (!rotation)
            {
                return fail<transform>(std::format("Scene \"{}\", node \"{}\": rotation must be 4 numbers [x, y, z, w]", scene_id, node_name));
            }
            const std::optional<std::array<float, 3>> scale = read_floats_or<3>(data, "scale", identity.scale);
            if (!scale)
            {
                return fail<transform>(std::format("Scene \"{}\", node \"{}\": scale must be 3 numbers", scene_id, node_name));
            }

            return { transform{ *translation, *rotation, *scale }, {} };
        }

        //-------------------------------------------------------------------------
        // Turns a path written in a scene file (relative to that file) into a
        // content-relative id: "../meshes/barrel.glb" in
        // "a/scenes/barrels.scene.json" becomes "a/meshes/barrel.glb"
        std::string make_asset_id(std::string_view scene_id, const std::string& relative_path)
        {
            const std::filesystem::path scene_folder = std::filesystem::path(std::u8string(scene_id.begin(), scene_id.end())).parent_path();
            const std::filesystem::path relative = std::filesystem::path(std::u8string(relative_path.begin(), relative_path.end()));
            const std::u8string id = (scene_folder / relative).lexically_normal().generic_u8string();

            return std::string(id.begin(), id.end());
        }

        //-------------------------------------------------------------------------
        // Reads one entry of "nodes"
        read_result<node> read_node(const json_value& data, std::string_view scene_id)
        {
            const std::optional<std::string> name = data.get_kind() == json_kind::object ? read_string(data, "name") : std::nullopt;
            if (!name)
            {
                return fail<node>(std::format("Scene \"{}\": every node needs a \"name\"", scene_id));
            }

            node result;
            result.name = *name;

            // A node has either a "mesh" or a "scene" path, never both
            const std::optional<std::string> mesh = read_string(data, "mesh");
            const std::optional<std::string> child_scene = read_string(data, "scene");
            if (mesh.has_value() == child_scene.has_value())
            {
                return fail<node>(std::format("Scene \"{}\", node \"{}\": needs either a \"mesh\" or a \"scene\" path, not both or neither", scene_id, result.name));
            }
            result.kind = mesh ? node_kind::model : node_kind::scene;
            result.asset_id = make_asset_id(scene_id, mesh ? *mesh : *child_scene);

            read_result<transform> placement = read_transform(data, scene_id, result.name);
            if (!placement.value)
            {
                return fail<node>(std::move(placement.error));
            }
            result.local_transform = *placement.value;

            return { std::move(result), {} };
        }

        //-------------------------------------------------------------------------
        parse_result reject(std::string error)
        {
            return { std::nullopt, std::move(error) };
        }
    }

    //-------------------------------------------------------------------------
    parse_result parse(const json_value& root, std::string_view scene_id)
    {
        if (root.get_kind() != json_kind::object)
        {
            return reject(std::format("Scene \"{}\" is not a json object", scene_id));
        }

        // The header says which format and version the file is in
        const std::optional<std::string> format = read_string(root, "format");
        if (format != k_format)
        {
            return reject(std::format("Scene \"{}\" has no \"format\": \"{}\"", scene_id, k_format));
        }
        const std::unique_ptr<json_value> version = root.get_member("version");
        const bool is_supported_version = version != nullptr && version->get_kind() == json_kind::integer && version->get_integer() == k_supported_version;
        if (!is_supported_version)
        {
            return reject(std::format("Scene \"{}\" is not version {}, the only version this reader supports", scene_id, k_supported_version));
        }
        const std::optional<std::string> name = read_string(root, "name");
        if (!name)
        {
            return reject(std::format("Scene \"{}\" has no \"name\"", scene_id));
        }
        const std::unique_ptr<json_value> nodes = root.get_member("nodes");
        if (nodes == nullptr || nodes->get_kind() != json_kind::array)
        {
            return reject(std::format("Scene \"{}\" has no \"nodes\" array", scene_id));
        }

        scene result;
        result.name = *name;
        result.nodes.reserve(nodes->get_size());

        // One malformed node rejects the whole file, so the mistake shows up
        // when the file loads instead of as a level with a part missing
        for (uint32_t index = 0; index < nodes->get_size(); ++index)
        {
            const std::unique_ptr<json_value> entry = nodes->get_element(index);
            if (entry == nullptr)
            {
                return reject(std::format("Scene \"{}\": node {} could not be read", scene_id, index));
            }
            read_result<node> parsed = read_node(*entry, scene_id);
            if (!parsed.value)
            {
                return reject(std::move(parsed.error));
            }
            result.nodes.push_back(std::move(*parsed.value));
        }

        return { std::move(result), {} };
    }
}
