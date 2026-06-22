#include "LSystemMaterialParse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
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

enum class NamedValueKind
{
    Scalar,
    FloatList,
    Texture,
};

struct NamedValue
{
    NamedValueKind kind = NamedValueKind::Scalar;
    float scalar = 0.f;
    std::vector<float> floats;
    TextureDef texture;
};

using NamedProps = std::map<std::string, NamedValue>;

bool is_named_property_start(std::string_view line, std::size_t pos)
{
    std::size_t peek = pos;
    std::string id;
    if (!parse_identifier(line, peek, id))
    {
        return false;
    }
    skip_ws(line, peek);
    return peek < line.size() && line[peek] == ':';
}

bool is_texture_kind(std::string_view kind)
{
    return kind == "Grid" || kind == "Stripe" || kind == "Noise";
}

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
            material_parse_error(line, "expected number");
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
        material_parse_error(line, "expected ',' or '}'");
    }
}

void validate_grid_params(std::string_view line, const std::vector<float>& params)
{
    if (params.size() < 8u || params.size() > 11u)
    {
        material_parse_error(
            line,
            "Grid texture requires 8 to 11 params "
            "(onR,onG,onB, offR,offG,offB, intensityOn,intensityOff, "
            "[freq or freqU], [freqV], [thickness])");
    }
}

void validate_stripe_params(std::string_view line, const std::vector<float>& params)
{
    if (params.size() < 9u || params.size() > 10u)
    {
        material_parse_error(
            line,
            "Stripe texture requires 9 to 10 params "
            "(onR,onG,onB, offR,offG,offB, intensityOn,intensityOff, freq, [thickness])");
    }
}

void validate_noise_params(std::string_view line, const std::vector<float>& params)
{
    if (params.size() < 9u || params.size() > 11u)
    {
        material_parse_error(
            line,
            "Noise texture requires 9 to 11 params "
            "(onR,onG,onB, offR,offG,offB, intensityOn,intensityOff, scale, [octaves], [seed])");
    }
}

bool parse_texture_block(std::string_view line, std::size_t& pos, TextureDef& out_texture);

bool parse_colon_value(std::string_view line, std::size_t& pos, NamedValue& out_value)
{
    skip_ws(line, pos);
    if (pos >= line.size())
    {
        material_parse_error(line, "expected value after ':'");
    }

    if (line[pos] == '{')
    {
        std::size_t peek = pos + 1;
        skip_ws(line, peek);
        if (peek < line.size())
        {
            std::string maybeKind;
            std::size_t kindPeek = peek;
            if (parse_identifier(line, kindPeek, maybeKind) && is_texture_kind(maybeKind))
            {
                out_value.kind = NamedValueKind::Texture;
                return parse_texture_block(line, pos, out_value.texture);
            }
        }

        out_value.kind = NamedValueKind::FloatList;
        return parse_brace_float_list(line, pos, out_value.floats);
    }

    float scalar = 0.f;
    if (!parse_float(line, pos, scalar))
    {
        material_parse_error(line, "expected number or '{' value");
    }
    out_value.kind = NamedValueKind::Scalar;
    out_value.scalar = scalar;
    return true;
}

void apply_rgb_or_gray(NamedValue& out_value, const NamedValue& source, std::string_view line, const char* propName)
{
    if (source.kind == NamedValueKind::Scalar)
    {
        out_value.kind = NamedValueKind::FloatList;
        out_value.floats = {source.scalar, source.scalar, source.scalar};
        return;
    }
    if (source.kind == NamedValueKind::FloatList)
    {
        if (source.floats.size() != 3u)
        {
            material_parse_error(line, "rgb property requires 3 floats or a scalar gray value");
        }
        out_value = source;
        return;
    }
    material_parse_error(line, "property requires rgb floats or scalar");
    (void)propName;
}

struct OnOffIntensities
{
    float onR = 1.f;
    float onG = 1.f;
    float onB = 1.f;
    float offR = 0.f;
    float offG = 0.f;
    float offB = 0.f;
    float intensityOn = 1.f;
    float intensityOff = 1.f;
};

void parse_on_off_intensities(
    std::string_view line,
    const NamedProps& props,
    OnOffIntensities& out)
{
    for (const auto& [key, value] : props)
    {
        if (key == "on")
        {
            NamedValue rgb;
            apply_rgb_or_gray(rgb, value, line, "on");
            out.onR = rgb.floats[0];
            out.onG = rgb.floats[1];
            out.onB = rgb.floats[2];
        }
        else if (key == "off")
        {
            NamedValue rgb;
            apply_rgb_or_gray(rgb, value, line, "off");
            out.offR = rgb.floats[0];
            out.offG = rgb.floats[1];
            out.offB = rgb.floats[2];
        }
        else if (key == "intensityOn")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "intensityOn expects a scalar");
            }
            out.intensityOn = value.scalar;
        }
        else if (key == "intensityOff")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "intensityOff expects a scalar");
            }
            out.intensityOff = value.scalar;
        }
    }
}

std::vector<float> append_on_off_intensities(const OnOffIntensities& colors)
{
    return {
        colors.onR,
        colors.onG,
        colors.onB,
        colors.offR,
        colors.offG,
        colors.offB,
        colors.intensityOn,
        colors.intensityOff,
    };
}

std::vector<float> build_grid_params(std::string_view line, const NamedProps& props)
{
    OnOffIntensities colors;
    parse_on_off_intensities(line, props, colors);

    float freqU = 8.f;
    float freqV = 8.f;
    float thickness = 0.05f;
    bool hasFreq = false;
    bool hasFreqU = false;
    bool hasFreqV = false;
    bool hasThickness = false;

    for (const auto& [key, value] : props)
    {
        if (key == "on" || key == "off" || key == "intensityOn" || key == "intensityOff")
        {
            continue;
        }
        else if (key == "freq")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "Grid freq expects a scalar");
            }
            hasFreq = true;
            freqU = value.scalar;
            freqV = value.scalar;
        }
        else if (key == "freqU")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "Grid freqU expects a scalar");
            }
            hasFreqU = true;
            freqU = value.scalar;
        }
        else if (key == "freqV")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "Grid freqV expects a scalar");
            }
            hasFreqV = true;
            freqV = value.scalar;
        }
        else if (key == "thickness")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "Grid thickness expects a scalar");
            }
            hasThickness = true;
            thickness = value.scalar;
        }
        else
        {
            material_parse_error(line, "unknown Grid texture property");
        }
    }

    if (hasFreqU && !hasFreqV && !hasFreq)
    {
        freqV = freqU;
    }
    if (hasFreq && !hasFreqU)
    {
        freqU = freqV;
    }

    std::vector<float> params = append_on_off_intensities(colors);
    if (hasFreqV && (hasFreqU || hasFreq))
    {
        params.push_back(freqU);
        params.push_back(freqV);
        params.push_back(thickness);
    }
    else if (hasFreq || hasFreqU || hasThickness)
    {
        params.push_back(freqU);
        params.push_back(thickness);
    }
    return params;
}

std::vector<float> build_stripe_params(std::string_view line, const NamedProps& props)
{
    OnOffIntensities colors;
    parse_on_off_intensities(line, props, colors);

    float freq = 0.f;
    float thickness = 0.05f;
    bool hasFreq = false;
    bool hasThickness = false;

    for (const auto& [key, value] : props)
    {
        if (key == "on" || key == "off" || key == "intensityOn" || key == "intensityOff")
        {
            continue;
        }
        else if (key == "freq")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "Stripe freq expects a scalar");
            }
            hasFreq = true;
            freq = value.scalar;
        }
        else if (key == "thickness")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "Stripe thickness expects a scalar");
            }
            hasThickness = true;
            thickness = value.scalar;
        }
        else
        {
            material_parse_error(line, "unknown Stripe texture property");
        }
    }

    if (!hasFreq)
    {
        material_parse_error(line, "Stripe texture requires freq");
    }

    std::vector<float> params = append_on_off_intensities(colors);
    params.push_back(freq);
    if (hasThickness)
    {
        params.push_back(thickness);
    }
    return params;
}

std::vector<float> build_noise_params(std::string_view line, const NamedProps& props)
{
    OnOffIntensities colors;
    parse_on_off_intensities(line, props, colors);

    float scale = 0.f;
    float octaves = 1.f;
    float seed = 0.f;
    bool hasScale = false;
    bool hasOctaves = false;
    bool hasSeed = false;

    for (const auto& [key, value] : props)
    {
        if (key == "on" || key == "off" || key == "intensityOn" || key == "intensityOff")
        {
            continue;
        }
        else if (key == "scale")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "Noise scale expects a scalar");
            }
            hasScale = true;
            scale = value.scalar;
        }
        else if (key == "octaves")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "Noise octaves expects a scalar");
            }
            hasOctaves = true;
            octaves = value.scalar;
        }
        else if (key == "seed")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "Noise seed expects a scalar");
            }
            hasSeed = true;
            seed = value.scalar;
        }
        else
        {
            material_parse_error(line, "unknown Noise texture property");
        }
    }

    if (!hasScale)
    {
        material_parse_error(line, "Noise texture requires scale");
    }

    std::vector<float> params = append_on_off_intensities(colors);
    params.push_back(scale);
    if (hasOctaves || hasSeed)
    {
        params.push_back(octaves);
        params.push_back(seed);
    }
    return params;
}

std::vector<float> build_texture_params_from_named(
    std::string_view line, const std::string& kind, const NamedProps& props)
{
    if (kind == "Grid")
    {
        return build_grid_params(line, props);
    }
    if (kind == "Stripe")
    {
        return build_stripe_params(line, props);
    }
    if (kind == "Noise")
    {
        return build_noise_params(line, props);
    }
    material_parse_error(line, "unknown texture kind");
}

bool parse_named_property_entries(std::string_view line, std::size_t& pos, NamedProps& out_props)
{
    out_props.clear();
    for (;;)
    {
        skip_ws(line, pos);
        if (pos < line.size() && line[pos] == '}')
        {
            if (out_props.empty())
            {
                material_parse_error(line, "named property block requires at least one property");
            }
            return true;
        }

        std::string key;
        if (!parse_identifier(line, pos, key))
        {
            material_parse_error(line, "named property block expected identifier");
        }
        if (out_props.find(key) != out_props.end())
        {
            material_parse_error(line, "duplicate property key");
        }

        skip_ws(line, pos);
        if (pos >= line.size() || line[pos] != ':')
        {
            material_parse_error(line, "named property expected ':' after key");
        }
        ++pos;

        NamedValue value;
        if (!parse_colon_value(line, pos, value))
        {
            material_parse_error(line, "named property expected value");
        }
        out_props.emplace(key, std::move(value));

        skip_ws(line, pos);
        if (pos < line.size() && line[pos] == ',')
        {
            ++pos;
            continue;
        }
        if (pos < line.size() && line[pos] == '}')
        {
            if (out_props.empty())
            {
                material_parse_error(line, "named property block requires at least one property");
            }
            return true;
        }
        material_parse_error(line, "named property block expected ',' or '}'");
    }
}

bool parse_named_property_map(std::string_view line, std::size_t& pos, NamedProps& out_props)
{
    skip_ws(line, pos);
    if (pos >= line.size() || line[pos] != '{')
    {
        return false;
    }
    ++pos;

    if (!parse_named_property_entries(line, pos, out_props))
    {
        return false;
    }

    skip_ws(line, pos);
    if (pos >= line.size() || line[pos] != '}')
    {
        material_parse_error(line, "named property block expected closing '}'");
    }
    ++pos;
    return true;
}

void finalize_named_texture(
    std::string_view line, const std::string& kind, const NamedProps& props, TextureDef& out_texture)
{
    std::vector<float> params = build_texture_params_from_named(line, kind, props);
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

    if (!is_texture_kind(kind))
    {
        material_parse_error(line, "unknown texture kind");
    }

    skip_ws(line, pos);
    if (pos < line.size() && line[pos] == '{')
    {
        NamedProps props;
        if (!parse_named_property_map(line, pos, props))
        {
            material_parse_error(line, "texture block expected named properties");
        }
        finalize_named_texture(line, kind, props, out_texture);
        skip_ws(line, pos);
        if (pos >= line.size() || line[pos] != '}')
        {
            material_parse_error(line, "texture block expected closing '}'");
        }
        ++pos;
        return true;
    }

    if (pos >= line.size() || line[pos] != ',')
    {
        material_parse_error(line, "texture block expected ',' or '{' after kind");
    }
    ++pos;
    skip_ws(line, pos);

    if (is_named_property_start(line, pos))
    {
        NamedProps props;
        if (!parse_named_property_entries(line, pos, props))
        {
            material_parse_error(line, "texture block expected named properties");
        }
        finalize_named_texture(line, kind, props, out_texture);
        skip_ws(line, pos);
        if (pos >= line.size() || line[pos] != '}')
        {
            material_parse_error(line, "texture block expected closing '}'");
        }
        ++pos;
        return true;
    }

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

bool is_named_material_block(std::string_view line, std::size_t pos)
{
    skip_ws(line, pos);
    if (pos >= line.size() || line[pos] != '{')
    {
        return false;
    }
    ++pos;
    skip_ws(line, pos);
    return is_named_property_start(line, pos);
}

bool parse_named_material_block(std::string_view line, std::size_t& pos, NamedProps& out_props)
{
    return parse_named_property_map(line, pos, out_props);
}

enum class MaterialProperty
{
    Albedo,
    Roughness,
    Metallic,
    Transmission,
    Thin,
    Ior,
    Subsurface,
    Emission,
    DiffuseRoughness,
    ScatterRadiusR,
    ScatterRadiusG,
    ScatterRadiusB,
    Specular,
    Unknown,
};

MaterialProperty resolve_material_property(std::string_view key)
{
    if (key == "albedo" || key == "alb")
    {
        return MaterialProperty::Albedo;
    }
    if (key == "roughness" || key == "rou")
    {
        return MaterialProperty::Roughness;
    }
    if (key == "metallic" || key == "met")
    {
        return MaterialProperty::Metallic;
    }
    if (key == "transmission" || key == "trans")
    {
        return MaterialProperty::Transmission;
    }
    if (key == "thin")
    {
        return MaterialProperty::Thin;
    }
    if (key == "ior")
    {
        return MaterialProperty::Ior;
    }
    if (key == "subsurface" || key == "subs")
    {
        return MaterialProperty::Subsurface;
    }
    if (key == "emission" || key == "emiss" || key == "em")
    {
        return MaterialProperty::Emission;
    }
    if (key == "diffuseRoughness" || key == "diffuse" || key == "diffRou")
    {
        return MaterialProperty::DiffuseRoughness;
    }
    if (key == "scatterRadiusR" || key == "scatterR" || key == "sR")
    {
        return MaterialProperty::ScatterRadiusR;
    }
    if (key == "scatterRadiusG" || key == "scatterG" || key == "sG")
    {
        return MaterialProperty::ScatterRadiusG;
    }
    if (key == "scatterRadiusB" || key == "scatterB" || key == "sB")
    {
        return MaterialProperty::ScatterRadiusB;
    }
    if (key == "specular" || key == "spec")
    {
        return MaterialProperty::Specular;
    }
    return MaterialProperty::Unknown;
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

void assign_named_scalar_or_texture(
    MaterialChannel& channel, std::size_t slotIndex, const NamedValue& value, std::string_view line)
{
    if (value.kind == NamedValueKind::Texture)
    {
        set_texture_channel(channel, value.texture);
        return;
    }
    if (value.kind != NamedValueKind::Scalar)
    {
        material_parse_error(line, "channel expects scalar or texture value");
    }
    assign_scalar_channel(channel, slotIndex, value.scalar);
}

void assign_named_entry(std::string_view line, MaterialEntry& entry, const NamedProps& props)
{
    assign_legacy_entry(entry, {});

    for (const auto& [key, value] : props)
    {
        switch (resolve_material_property(key))
        {
        case MaterialProperty::Albedo:
            if (value.kind == NamedValueKind::Texture)
            {
                set_texture_channel(entry.albedo, value.texture);
            }
            else if (value.kind == NamedValueKind::Scalar)
            {
                entry.albedo.mode = MaterialChannel::Mode::Inline;
                const float gray = clamp01(value.scalar);
                entry.albedo.r = gray;
                entry.albedo.g = gray;
                entry.albedo.b = gray;
            }
            else if (value.kind == NamedValueKind::FloatList)
            {
                if (value.floats.size() != 3u)
                {
                    material_parse_error(line, "albedo requires 3 floats or a scalar gray value");
                }
                entry.albedo.mode = MaterialChannel::Mode::Inline;
                entry.albedo.r = clamp01(value.floats[0]);
                entry.albedo.g = clamp01(value.floats[1]);
                entry.albedo.b = clamp01(value.floats[2]);
            }
            break;
        case MaterialProperty::Roughness:
            assign_named_scalar_or_texture(entry.roughness, 1u, value, line);
            break;
        case MaterialProperty::Metallic:
            assign_named_scalar_or_texture(entry.metallic, 2u, value, line);
            break;
        case MaterialProperty::Transmission:
            assign_named_scalar_or_texture(entry.transmission, 3u, value, line);
            break;
        case MaterialProperty::Thin:
            assign_named_scalar_or_texture(entry.thin, 4u, value, line);
            break;
        case MaterialProperty::Ior:
            assign_named_scalar_or_texture(entry.ior, 5u, value, line);
            break;
        case MaterialProperty::Subsurface:
            assign_named_scalar_or_texture(entry.subsurface, 6u, value, line);
            break;
        case MaterialProperty::Emission:
            assign_named_scalar_or_texture(entry.emission, 7u, value, line);
            break;
        case MaterialProperty::DiffuseRoughness:
            assign_named_scalar_or_texture(entry.diffuseRoughness, 8u, value, line);
            break;
        case MaterialProperty::ScatterRadiusR:
            assign_named_scalar_or_texture(entry.scatterRadiusR, 9u, value, line);
            break;
        case MaterialProperty::ScatterRadiusG:
            assign_named_scalar_or_texture(entry.scatterRadiusG, 10u, value, line);
            break;
        case MaterialProperty::ScatterRadiusB:
            assign_named_scalar_or_texture(entry.scatterRadiusB, 11u, value, line);
            break;
        case MaterialProperty::Specular:
            assign_named_scalar_or_texture(entry.specular, 12u, value, line);
            break;
        case MaterialProperty::Unknown:
            material_parse_error(line, "unknown material property");
            break;
        }
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

    MaterialDefinition def;
    def.id = id;

    if (is_named_material_block(trimmed_line, pos))
    {
        NamedProps props;
        if (!parse_named_material_block(trimmed_line, pos, props))
        {
            material_parse_error(
                trimmed_line,
                "material declaration expected named properties "
                "(e.g. albedo: {r,g,b}, roughness: 0.5, emission: 1.0)");
        }

        skip_ws(trimmed_line, pos);
        if (pos != trimmed_line.size())
        {
            material_parse_error(trimmed_line, "unexpected text after material declaration");
        }

        if (material_id_defined(definitions, id))
        {
            material_parse_error(trimmed_line, "duplicate material id");
        }

        assign_named_entry(trimmed_line, def.entry, props);
        definitions.push_back(def);
        return true;
    }

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
