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