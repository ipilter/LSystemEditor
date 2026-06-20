#include "LSystemMaterialParse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

[[noreturn]] void material_parse_error(std::string_view line, const char* message)
{
    throw std::runtime_error(std::string(message) + ": " + std::string(line));
}

float clamp01(float v)
{
    return std::clamp(v, 0.f, 1.f);
}

void skip_ws(std::string_view line, std::size_t& pos)
{
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
    {
        ++pos;
    }
}

bool parse_material_id_token(std::string_view line, std::size_t& pos, std::string& out_id)
{
    skip_ws(line, pos);
    if (pos >= line.size())
    {
        return false;
    }

    const std::size_t start = pos;
    const unsigned char first = static_cast<unsigned char>(line[pos]);
    if (std::isdigit(first))
    {
        while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])))
        {
            ++pos;
        }
    }
    else if (std::isalpha(first) || line[pos] == '_')
    {
        ++pos;
        while (pos < line.size())
        {
            const unsigned char c = static_cast<unsigned char>(line[pos]);
            if (std::isalnum(c) || line[pos] == '_')
            {
                ++pos;
            }
            else
            {
                break;
            }
        }
    }
    else
    {
        return false;
    }

    if (pos == start)
    {
        return false;
    }

    out_id.assign(line.data() + start, pos - start);
    return true;
}

bool parse_mat_id_paren(std::string_view line, std::size_t& pos, std::string& out_id)
{
    if (line.size() < 3 || line[0] != 'M' || line[1] != 'a' || line[2] != 't')
    {
        return false;
    }
    pos = 3;
    skip_ws(line, pos);
    if (pos >= line.size() || line[pos] != '(')
    {
        return false;
    }
    ++pos;
    if (!parse_material_id_token(line, pos, out_id))
    {
        return false;
    }
    skip_ws(line, pos);
    if (pos >= line.size() || line[pos] != ')')
    {
        return false;
    }
    ++pos;
    return true;
}

bool parse_float(std::string_view line, std::size_t& pos, float& out)
{
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
    {
        ++pos;
    }
    if (pos >= line.size())
    {
        return false;
    }
    const char* begin = line.data() + pos;
    char* end = nullptr;
    const double v = std::strtod(begin, &end);
    if (end == begin)
    {
        return false;
    }
    pos = static_cast<std::size_t>(end - line.data());
    out = static_cast<float>(v);
    return true;
}

bool parse_identifier(std::string_view line, std::size_t& pos, std::string& out_id)
{
    return parse_material_id_token(line, pos, out_id);
}

struct ParsedComponent
{
    bool isTexture = false;
    TextureDef texture;
    float scalar = 0.f;
};

bool parse_brace_float_list(std::string_view line, std::size_t& pos, std::vector<float>& comps)
{
    skip_ws(line, pos);
    if (pos >= line.size() || line[pos] != '{')
    {
        return false;
    }
    ++pos;

    comps.clear();
    for (;;)
    {
        skip_ws(line, pos);
        if (pos < line.size() && line[pos] == '}')
        {
            ++pos;
            return true;
        }
        float v = 0.f;
        if (!parse_float(line, pos, v))
        {
            material_parse_error(line, "texture block expected number");
        }
        comps.push_back(v);
        skip_ws(line, pos);
        if (pos < line.size() && line[pos] == ',')
        {
            ++pos;
            continue;
        }
        if (pos < line.size() && line[pos] == '}')
        {
            ++pos;
            return true;
        }
        material_parse_error(line, "texture block expected ',' or '}'");
    }
}

void validate_grid_params(std::string_view line, const std::vector<float>& params)
{
    if (params.size() < 6u || params.size() > 10u)
    {
        material_parse_error(
            line,
            "Grid texture requires 6 to 10 params "
            "(ar, ag, ab, br, bg, bb, [freq or freqU], [freqV], [lineThickness])");
    }
}

void validate_stripe_params(std::string_view line, const std::vector<float>& params)
{
    if (params.size() < 2u || params.size() > 4u)
    {
        material_parse_error(
            line,
            "Stripe texture requires 2 to 4 params "
            "(frequency, lineThickness, [onValue], [offValue])");
    }
}

void validate_noise_params(std::string_view line, const std::vector<float>& params)
{
    if (params.size() < 1u || params.size() > 5u)
    {
        material_parse_error(
            line,
            "Noise texture requires 1 to 5 params "
            "(scale, [octaves], [seed], [minValue], [maxValue])");
    }
}

bool parse_texture_block(std::string_view line, std::size_t& pos, TextureDef& out_texture)
{
    skip_ws(line, pos);
    if (pos >= line.size() || line[pos] != '{')
    {
        return false;
    }
    ++pos;

    std::string kind;
    if (!parse_identifier(line, pos, kind))
    {
        return false;
    }

    skip_ws(line, pos);
    if (pos >= line.size() || line[pos] != ',')
    {
        material_parse_error(line, "texture block expected ',' after kind");
    }
    ++pos;

    std::vector<float> params;
    for (;;)
    {
        skip_ws(line, pos);
        if (pos < line.size() && line[pos] == '}')
        {
            ++pos;
            break;
        }

        float value = 0.f;
        if (!parse_float(line, pos, value))
        {
            material_parse_error(line, "texture block expected number");
        }
        params.push_back(value);

        skip_ws(line, pos);
        if (pos < line.size() && line[pos] == ',')
        {
            ++pos;
            continue;
        }
        if (pos < line.size() && line[pos] == '}')
        {
            ++pos;
            break;
        }
        material_parse_error(line, "texture block expected ',' or '}'");
    }

    if (kind == "Grid")
    {
        validate_grid_params(line, params);
    }
    else if (kind == "Stripe")
    {
        validate_stripe_params(line, params);
    }
    else if (kind == "Noise")
    {
        validate_noise_params(line, params);
    }
    else
    {
        material_parse_error(line, "unknown texture kind");
    }

    out_texture.kind = kind;
    out_texture.params = std::move(params);
    return true;
}

bool parse_component(std::string_view line, std::size_t& pos, ParsedComponent& out_component)
{
    skip_ws(line, pos);
    if (pos >= line.size())
    {
        return false;
    }

    if (line[pos] == '{')
    {
        std::size_t peek = pos + 1;
        skip_ws(line, peek);
        if (peek < line.size())
        {
            const unsigned char next = static_cast<unsigned char>(line[peek]);
            if (std::isalpha(next) || line[peek] == '_')
            {
                out_component.isTexture = true;
                return parse_texture_block(line, pos, out_component.texture);
            }
        }
    }

    float value = 0.f;
    if (!parse_float(line, pos, value))
    {
        return false;
    }

    out_component.scalar = value;
    return true;
}

bool parse_brace_component_list(std::string_view line, std::size_t& pos, std::vector<ParsedComponent>& comps)
{
    skip_ws(line, pos);
    if (pos >= line.size() || line[pos] != '{')
    {
        return false;
    }
    ++pos;

    comps.clear();
    for (;;)
    {
        skip_ws(line, pos);
        if (pos < line.size() && line[pos] == '}')
        {
            ++pos;
            return true;
        }

        ParsedComponent component;
        if (!parse_component(line, pos, component))
        {
            material_parse_error(line, "material declaration expected component");
        }
        comps.push_back(component);

        skip_ws(line, pos);
        if (pos < line.size() && line[pos] == ',')
        {
            ++pos;
            continue;
        }
        if (pos < line.size() && line[pos] == '}')
        {
            ++pos;
            return true;
        }
        material_parse_error(line, "material declaration expected ',' or '}'");
    }
}

bool all_scalar_components(const std::vector<ParsedComponent>& comps)
{
    for (const ParsedComponent& component : comps)
    {
        if (component.isTexture)
        {
            return false;
        }
    }
    return true;
}

void set_inline_scalar(MaterialChannel& channel, float value)
{
    channel.mode = MaterialChannel::Mode::Inline;
    channel.scalar = value;
}

void set_texture_channel(MaterialChannel& channel, const TextureDef& texture)
{
    channel.mode = MaterialChannel::Mode::Texture;
    channel.texture = texture;
}

void assign_legacy_entry(MaterialEntry& entry, const std::vector<float>& comps)
{
    entry.albedo.mode = MaterialChannel::Mode::Inline;
    if (comps.size() >= 3u)
    {
        entry.albedo.r = clamp01(comps[0]);
        entry.albedo.g = clamp01(comps[1]);
        entry.albedo.b = clamp01(comps[2]);
    }
    else
    {
        entry.albedo.r = 0.8f;
        entry.albedo.g = 0.8f;
        entry.albedo.b = 0.8f;
    }

    const auto scalarAt = [&comps](std::size_t index, float defaultValue) -> float {
        return index < comps.size() ? comps[index] : defaultValue;
    };

    set_inline_scalar(entry.roughness, clamp01(scalarAt(3u, 0.5f)));
    set_inline_scalar(entry.metallic, clamp01(scalarAt(4u, 0.f)));
    set_inline_scalar(entry.transmission, clamp01(scalarAt(5u, 0.f)));
    set_inline_scalar(entry.thin, clamp01(scalarAt(6u, 0.f)));
    set_inline_scalar(entry.ior, std::max(1.0e-3f, scalarAt(7u, 1.5f)));
    set_inline_scalar(entry.subsurface, clamp01(scalarAt(8u, 0.f)));
    set_inline_scalar(entry.emission, std::max(0.f, scalarAt(9u, 0.f)));
    const float roughnessDefault = materialChannelScalar(entry.roughness, 0.5f);
    if (comps.size() > 10u) {
        set_inline_scalar(entry.diffuseRoughness, clamp01(comps[10]));
    } else {
        set_inline_scalar(entry.diffuseRoughness, -1.f);
    }
    set_inline_scalar(entry.scatterRadiusR, std::max(0.f, scalarAt(11u, 0.f)));
    set_inline_scalar(entry.scatterRadiusG, std::max(0.f, scalarAt(12u, 0.f)));
    set_inline_scalar(entry.scatterRadiusB, std::max(0.f, scalarAt(13u, 0.f)));
    set_inline_scalar(entry.specular, clamp01(scalarAt(14u, 1.f)));
}

void assign_scalar_channel(MaterialChannel& channel, std::size_t slotIndex, float value)
{
    if (slotIndex == 5u)
    {
        set_inline_scalar(channel, std::max(1.0e-3f, value));
    }
    else if (slotIndex == 7u)
    {
        set_inline_scalar(channel, std::max(0.f, value));
    }
    else if (slotIndex >= 9u && slotIndex <= 11u)
    {
        set_inline_scalar(channel, std::max(0.f, value));
    }
    else
    {
        set_inline_scalar(channel, clamp01(value));
    }
}

void assign_component_entry(std::string_view line, MaterialEntry& entry, const std::vector<ParsedComponent>& comps)
{
    assign_legacy_entry(entry, {});

    std::size_t index = 0;
    if (comps.empty())
    {
        return;
    }

    if (comps[0].isTexture)
    {
        set_texture_channel(entry.albedo, comps[0].texture);
        index = 1;
    }
    else
    {
        if (comps.size() < 3u)
        {
            material_parse_error(line, "material albedo requires 3 floats or a texture block");
        }
        entry.albedo.mode = MaterialChannel::Mode::Inline;
        entry.albedo.r = clamp01(comps[0].scalar);
        entry.albedo.g = clamp01(comps[1].scalar);
        entry.albedo.b = clamp01(comps[2].scalar);
        index = 3;
    }

    MaterialChannel* slots[] = {
        &entry.roughness,
        &entry.metallic,
        &entry.transmission,
        &entry.thin,
        &entry.ior,
        &entry.subsurface,
        &entry.emission,
        &entry.diffuseRoughness,
        &entry.scatterRadiusR,
        &entry.scatterRadiusG,
        &entry.scatterRadiusB,
        &entry.specular,
    };

    for (std::size_t slot = 0; slot < 11u && index < comps.size(); ++slot, ++index)
    {
        const ParsedComponent& component = comps[index];
        if (component.isTexture)
        {
            set_texture_channel(*slots[slot], component.texture);
        }
        else
        {
            assign_scalar_channel(*slots[slot], slot + 1u, component.scalar);
        }
    }
}

void validate_component_count(std::string_view line, const std::vector<ParsedComponent>& comps)
{
    if (comps.empty() || comps.size() > 15u)
    {
        material_parse_error(
            line,
            "material requires 1 to 15 components "
            "(albedo texture or r,g,b, then optional channel values)");
    }

    if (all_scalar_components(comps))
    {
        if (comps.size() < 3u)
        {
            material_parse_error(
                line,
                "material requires 3 to 15 scalar components "
                "(r, g, b, [roughness], [metallic], [transmission], [thin], [ior], "
                "[subsurface], [emission], [diffuseRoughness], [scatterRadiusR], "
                "[scatterRadiusG], [scatterRadiusB], [specular])");
        }
        return;
    }

    if (!comps[0].isTexture && comps.size() < 3u)
    {
        material_parse_error(line, "material albedo requires 3 floats or a texture block");
    }
}

void assign_material_entry(std::string_view line, MaterialEntry& entry, const std::vector<ParsedComponent>& comps)
{
    if (all_scalar_components(comps))
    {
        std::vector<float> legacyValues;
        legacyValues.reserve(comps.size());
        for (const ParsedComponent& component : comps)
        {
            legacyValues.push_back(component.scalar);
        }
        assign_legacy_entry(entry, legacyValues);
        return;
    }

    assign_component_entry(line, entry, comps);
}

} // namespace

bool is_material_declaration_line(std::string_view trimmed_line)
{
    std::size_t pos = 0;
    std::string id;
    if (!parse_mat_id_paren(trimmed_line, pos, id))
    {
        return false;
    }
    skip_ws(trimmed_line, pos);
    if (pos >= trimmed_line.size() || trimmed_line[pos] != '=')
    {
        return false;
    }
    ++pos;
    skip_ws(trimmed_line, pos);
    return pos < trimmed_line.size() && trimmed_line[pos] == '{';
}

bool material_id_defined(const std::vector<MaterialDefinition>& definitions, const std::string& id)
{
    for (const MaterialDefinition& def : definitions)
    {
        if (def.id == id)
        {
            return true;
        }
    }
    return false;
}

bool try_parse_material_line(std::string_view trimmed_line, std::vector<MaterialDefinition>& definitions)
{
    if (trimmed_line.empty())
    {
        return false;
    }

    std::size_t pos = 0;
    std::string id;
    if (!parse_mat_id_paren(trimmed_line, pos, id))
    {
        return false;
    }

    skip_ws(trimmed_line, pos);
    if (pos >= trimmed_line.size() || trimmed_line[pos] != '=')
    {
        return false;
    }
    ++pos;
    skip_ws(trimmed_line, pos);

    std::vector<ParsedComponent> comps;
    if (!parse_brace_component_list(trimmed_line, pos, comps))
    {
        material_parse_error(
            trimmed_line,
            "material declaration expected '{' "
            "(r, g, b, [roughness], [metallic], [transmission], [thin], [ior], [subsurface], "
            "[emission], [diffuseRoughness], [scatterRadiusR], [scatterRadiusG], [scatterRadiusB], [specular])");
    }

    skip_ws(trimmed_line, pos);
    if (pos != trimmed_line.size())
    {
        material_parse_error(trimmed_line, "unexpected text after material declaration");
    }

    validate_component_count(trimmed_line, comps);

    if (material_id_defined(definitions, id))
    {
        material_parse_error(trimmed_line, "duplicate material id");
    }

    MaterialDefinition def;
    def.id = id;
    assign_material_entry(trimmed_line, def.entry, comps);
    definitions.push_back(def);
    return true;
}

void parse_materials_from_lines(const std::vector<std::string_view>& lines,
    std::vector<MaterialDefinition>& definitions)
{
    for (const std::string_view raw : lines)
    {
        std::string_view t = raw;
        const std::size_t hash = t.find('#');
        if (hash != std::string_view::npos)
        {
            t = t.substr(0, hash);
        }
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front())))
        {
            t.remove_prefix(1);
        }
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
        {
            t.remove_suffix(1);
        }
        if (t.empty())
        {
            continue;
        }
        if (is_material_declaration_line(t))
        {
            const bool parsed = try_parse_material_line(t, definitions);
            (void)parsed;
        }
    }
}
