#include "LSystemMaterialParse.h"

#include "LSystemMaterials.h"

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

enum class NamedValueKind
{
    Scalar,
    FloatList,
    Texture,
    Identifier,
    NestedProps,
};

struct NamedValue;

using NamedProps = std::map<std::string, NamedValue>;

struct NamedValue
{
    NamedValueKind kind = NamedValueKind::Scalar;
    float scalar = 0.f;
    std::vector<float> floats;
    TextureDef texture;
    std::string identifier;
    NamedProps nested;
};

bool parse_named_property_map(std::string_view line, std::size_t& pos, NamedProps& out_props);

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

        if (is_named_property_start(line, peek))
        {
            out_value.kind = NamedValueKind::NestedProps;
            return parse_named_property_map(line, pos, out_value.nested);
        }

        out_value.kind = NamedValueKind::FloatList;
        return parse_brace_float_list(line, pos, out_value.floats);
    }

    float scalar = 0.f;
    if (!parse_float(line, pos, scalar))
    {
        std::string identifier;
        if (!parse_identifier(line, pos, identifier))
        {
            material_parse_error(line, "expected number, identifier, or '{' value");
        }
        out_value.kind = NamedValueKind::Identifier;
        out_value.identifier = std::move(identifier);
        return true;
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
    if (hasFreqV && !hasFreqU && !hasFreq)
    {
        freqU = freqV;
    }
    if (hasFreq && !hasFreqU)
    {
        freqU = freqV;
    }

    std::vector<float> params = append_on_off_intensities(colors);
    if (hasFreqV || hasFreqU || hasFreq)
    {
        params.push_back(freqU);
        params.push_back(freqV);
        params.push_back(thickness);
    }
    else if (hasThickness)
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
    Type,
    Albedo,
    Roughness,
    Metallic,
    DiffuseRoughness,
    Specular,
    Emission,
    Subsurface,
    SubsurfaceRadius,
    SubsurfaceScatterScale,
    SigmaA,
    SigmaS,
    MediumG,
    Ior,
    Abbe,
    Unknown,
};

MaterialProperty resolve_material_property(std::string_view key)
{
    if (key == "type")
    {
        return MaterialProperty::Type;
    }
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
    if (key == "diffuseRoughness" || key == "diffuse" || key == "diffRou")
    {
        return MaterialProperty::DiffuseRoughness;
    }
    if (key == "specular" || key == "spec")
    {
        return MaterialProperty::Specular;
    }
    if (key == "emission" || key == "emiss" || key == "em")
    {
        return MaterialProperty::Emission;
    }
    if (key == "subsurface" || key == "ss")
    {
        return MaterialProperty::Subsurface;
    }
    if (key == "subsurfaceRadius" || key == "ssRadius")
    {
        return MaterialProperty::SubsurfaceRadius;
    }
    if (key == "scatterScale" || key == "subsurfaceScatterScale" || key == "ssScale")
    {
        return MaterialProperty::SubsurfaceScatterScale;
    }
    if (key == "sigmaA" || key == "absorption" || key == "abs")
    {
        return MaterialProperty::SigmaA;
    }
    if (key == "sigmaS" || key == "scattering" || key == "scatter" || key == "scat")
    {
        return MaterialProperty::SigmaS;
    }
    if (key == "g" || key == "anisotropy" || key == "asymmetry")
    {
        return MaterialProperty::MediumG;
    }
    if (key == "ior")
    {
        return MaterialProperty::Ior;
    }
    if (key == "abbe" || key == "dispersion")
    {
        return MaterialProperty::Abbe;
    }
    return MaterialProperty::Unknown;
}

void set_inline_scalar(MaterialChannel& channel, float value)
{
    channel.mode = MaterialChannel::Mode::Inline;
    channel.scalar = value;
}

void set_inline_rgb(MaterialChannel& channel, float r, float g, float b)
{
    channel.mode = MaterialChannel::Mode::Inline;
    channel.r = r;
    channel.g = g;
    channel.b = b;
}

void set_texture_channel(MaterialChannel& channel, const TextureDef& texture)
{
    channel.mode = MaterialChannel::Mode::Texture;
    channel.texture = texture;
}

void init_default_entry(MaterialEntry& entry)
{
    entry.typeName = "Opaque";
    set_inline_rgb(entry.albedo, 0.8f, 0.8f, 0.8f);
    set_inline_scalar(entry.roughness, 0.5f);
    set_inline_scalar(entry.metallic, 0.f);
    set_inline_scalar(entry.diffuseRoughness, -1.f);
    set_inline_scalar(entry.specular, 1.f);
    set_inline_scalar(entry.emission, 0.f);
    set_inline_scalar(entry.subsurface, 0.f);
    set_inline_scalar(entry.subsurfaceRadius, 1.f);
    set_inline_scalar(entry.subsurfaceScatterScale, 1.f);
    set_inline_rgb(entry.sigmaA, 0.f, 0.f, 0.f);
    set_inline_rgb(entry.sigmaS, 0.f, 0.f, 0.f);
    set_inline_scalar(entry.mediumG, 0.f);
    set_inline_scalar(entry.ior, 1.5f);
    set_inline_scalar(entry.abbe, 58.f);
}

void assign_material_type(MaterialEntry& entry, const NamedValue& value, std::string_view line)
{
    if (value.kind == NamedValueKind::Identifier)
    {
        entry.typeName = value.identifier;
        return;
    }
    if (value.kind == NamedValueKind::Scalar)
    {
        material_parse_error(
            line,
            "type expects identifier (Opaque, Glass, Subsurface, Emissive)");
    }
    material_parse_error(line, "type expects identifier value");
}

void reject_deprecated_volume_property(MaterialProperty property, std::string_view line)
{
    switch (property)
    {
    case MaterialProperty::SigmaA:
        material_parse_error(
            line,
            "sigmaA/absorption is deprecated; use subsurface with subsurfaceRadius (mm)");
    case MaterialProperty::SigmaS:
        material_parse_error(
            line,
            "sigmaS/scattering is deprecated; use subsurface with subsurfaceRadius (mm)");
    case MaterialProperty::MediumG:
        material_parse_error(
            line,
            "g/anisotropy is deprecated for volume materials; use subsurface block with anisotropy");
    default:
        break;
    }
}

void assign_scalar_channel(MaterialChannel& channel, MaterialProperty property, float value)
{
    switch (property)
    {
    case MaterialProperty::Ior:
        set_inline_scalar(channel, std::max(1.0e-3f, value));
        break;
    case MaterialProperty::Abbe:
        set_inline_scalar(channel, std::max(1.0f, value));
        break;
    case MaterialProperty::Emission:
        set_inline_scalar(channel, std::max(0.f, value));
        break;
    case MaterialProperty::MediumG:
        set_inline_scalar(channel, std::max(-1.f, std::min(1.f, value)));
        break;
    case MaterialProperty::DiffuseRoughness:
        set_inline_scalar(channel, value);
        break;
    case MaterialProperty::SubsurfaceScatterScale:
        set_inline_scalar(channel, std::max(1.0e-6f, value));
        break;
    default:
        set_inline_scalar(channel, clamp01(value));
        break;
    }
}

void assign_named_scalar_or_texture(
    MaterialChannel& channel, MaterialProperty property, const NamedValue& value, std::string_view line)
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
    assign_scalar_channel(channel, property, value.scalar);
}

void assign_named_rgb_or_texture(
    MaterialChannel& channel,
    const NamedValue& value,
    std::string_view line,
    bool clampAlbedo)
{
    if (value.kind == NamedValueKind::Texture)
    {
        set_texture_channel(channel, value.texture);
        return;
    }
    if (value.kind == NamedValueKind::Scalar)
    {
        const float v = clampAlbedo ? clamp01(value.scalar) : std::max(0.f, value.scalar);
        set_inline_rgb(channel, v, v, v);
        return;
    }
    if (value.kind != NamedValueKind::FloatList || value.floats.size() != 3u)
    {
        material_parse_error(line, "channel requires scalar, 3 floats, or a texture value");
    }
    if (clampAlbedo)
    {
        set_inline_rgb(
            channel,
            clamp01(value.floats[0]),
            clamp01(value.floats[1]),
            clamp01(value.floats[2]));
    }
    else
    {
        set_inline_rgb(
            channel,
            std::max(0.f, value.floats[0]),
            std::max(0.f, value.floats[1]),
            std::max(0.f, value.floats[2]));
    }
}

void assign_subsurface_block(MaterialEntry& entry, const NamedProps& props, std::string_view line)
{
    float radiusR = materialChannelR(entry.subsurfaceRadius);
    float radiusG = materialChannelG(entry.subsurfaceRadius);
    float radiusB = materialChannelB(entry.subsurfaceRadius);
    bool hasRadiusR = false;
    bool hasRadiusG = false;
    bool hasRadiusB = false;

    for (const auto& [key, value] : props)
    {
        if (key == "weight" || key == "subsurface" || key == "ss")
        {
            assign_named_scalar_or_texture(entry.subsurface, MaterialProperty::Subsurface, value, line);
        }
        else if (key == "scatterR" || key == "ssRadiusR")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "scatterR expects a scalar");
            }
            radiusR = std::max(0.f, value.scalar);
            hasRadiusR = true;
        }
        else if (key == "scatterG" || key == "ssRadiusG")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "scatterG expects a scalar");
            }
            radiusG = std::max(0.f, value.scalar);
            hasRadiusG = true;
        }
        else if (key == "scatterB" || key == "ssRadiusB")
        {
            if (value.kind != NamedValueKind::Scalar)
            {
                material_parse_error(line, "scatterB expects a scalar");
            }
            radiusB = std::max(0.f, value.scalar);
            hasRadiusB = true;
        }
        else if (key == "scatterScale" || key == "subsurfaceScatterScale" || key == "ssScale")
        {
            assign_named_scalar_or_texture(
                entry.subsurfaceScatterScale, MaterialProperty::SubsurfaceScatterScale, value, line);
        }
        else if (key == "anisotropy" || key == "g" || key == "asymmetry")
        {
            assign_named_scalar_or_texture(entry.mediumG, MaterialProperty::MediumG, value, line);
        }
        else if (key == "subsurfaceRadius" || key == "ssRadius")
        {
            assign_named_rgb_or_texture(entry.subsurfaceRadius, value, line, false);
            hasRadiusR = hasRadiusG = hasRadiusB = true;
        }
        else
        {
            material_parse_error(line, "unknown subsurface block property");
        }
    }

    if (hasRadiusR || hasRadiusG || hasRadiusB)
    {
        if (!hasRadiusR)
        {
            radiusR = materialChannelR(entry.subsurfaceRadius);
        }
        if (!hasRadiusG)
        {
            radiusG = materialChannelG(entry.subsurfaceRadius);
        }
        if (!hasRadiusB)
        {
            radiusB = materialChannelB(entry.subsurfaceRadius);
        }
        set_inline_rgb(entry.subsurfaceRadius, radiusR, radiusG, radiusB);
    }
}

void assign_named_entry(std::string_view line, MaterialEntry& entry, const NamedProps& props)
{
    init_default_entry(entry);

    for (const auto& [key, value] : props)
    {
        const MaterialProperty property = resolve_material_property(key);
        reject_deprecated_volume_property(property, line);

        switch (property)
        {
        case MaterialProperty::Type:
            assign_material_type(entry, value, line);
            break;
        case MaterialProperty::Albedo:
            assign_named_rgb_or_texture(entry.albedo, value, line, true);
            break;
        case MaterialProperty::Roughness:
            assign_named_scalar_or_texture(entry.roughness, MaterialProperty::Roughness, value, line);
            break;
        case MaterialProperty::Metallic:
            assign_named_scalar_or_texture(entry.metallic, MaterialProperty::Metallic, value, line);
            break;
        case MaterialProperty::DiffuseRoughness:
            assign_named_scalar_or_texture(entry.diffuseRoughness, MaterialProperty::DiffuseRoughness, value, line);
            break;
        case MaterialProperty::Specular:
            assign_named_scalar_or_texture(entry.specular, MaterialProperty::Specular, value, line);
            break;
        case MaterialProperty::Emission:
            assign_named_scalar_or_texture(entry.emission, MaterialProperty::Emission, value, line);
            break;
        case MaterialProperty::Subsurface:
            if (value.kind == NamedValueKind::NestedProps)
            {
                assign_subsurface_block(entry, value.nested, line);
            }
            else
            {
                assign_named_scalar_or_texture(entry.subsurface, MaterialProperty::Subsurface, value, line);
            }
            break;
        case MaterialProperty::SubsurfaceRadius:
            assign_named_rgb_or_texture(entry.subsurfaceRadius, value, line, false);
            break;
        case MaterialProperty::SubsurfaceScatterScale:
            assign_named_scalar_or_texture(
                entry.subsurfaceScatterScale, MaterialProperty::SubsurfaceScatterScale, value, line);
            break;
        case MaterialProperty::MediumG:
            assign_named_scalar_or_texture(entry.mediumG, MaterialProperty::MediumG, value, line);
            break;
        case MaterialProperty::Ior:
            assign_named_scalar_or_texture(entry.ior, MaterialProperty::Ior, value, line);
            break;
        case MaterialProperty::Abbe:
            assign_named_scalar_or_texture(entry.abbe, MaterialProperty::Abbe, value, line);
            break;
        case MaterialProperty::Unknown:
            material_parse_error(line, "unknown material property");
            break;
        }
    }
}

std::string_view strip_line_comment(std::string_view raw)
{
    const std::size_t hash = raw.find('#');
    if (hash != std::string_view::npos)
    {
        return raw.substr(0, hash);
    }
    return raw;
}

std::string_view trim_line(std::string_view t)
{
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front())))
    {
        t.remove_prefix(1);
    }
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
    {
        t.remove_suffix(1);
    }
    return t;
}

int net_brace_delta(std::string_view text)
{
    int delta = 0;
    for (char c : text)
    {
        if (c == '{')
        {
            ++delta;
        }
        else if (c == '}')
        {
            --delta;
        }
    }
    return delta;
}

} // namespace

bool is_material_declaration_start_line(std::string_view trimmed_line)
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

bool is_material_declaration_line(std::string_view trimmed_line)
{
    if (!is_material_declaration_start_line(trimmed_line))
    {
        return false;
    }
    return net_brace_delta(trimmed_line) == 0;
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

    skip_ws(trimmed_line, pos);
    if (pos >= trimmed_line.size() || trimmed_line[pos] != '{')
    {
        material_parse_error(trimmed_line, "material declaration expected '{'");
    }

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
        assign_named_entry(trimmed_line, def.entry, props);
    }
    else
    {
        std::size_t checkPos = pos;
        ++checkPos;
        skip_ws(trimmed_line, checkPos);
        if (checkPos < trimmed_line.size() && trimmed_line[checkPos] == '}')
        {
            pos = checkPos + 1;
            init_default_entry(def.entry);
        }
        else
        {
            material_parse_error(
                trimmed_line,
                "material declaration requires named properties "
                "(e.g. Mat(id) = {type: Glass, albedo: {r,g,b}, roughness: 0.5, ior: 1.5})");
        }
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

    definitions.push_back(def);
    return true;
}

void parse_materials_from_lines(const std::vector<std::string_view>& lines,
    std::vector<MaterialDefinition>& definitions)
{
    std::string blockBuffer;
    int braceDepth = 0;
    bool inBlock = false;

    for (const std::string_view raw : lines)
    {
        const std::string_view t = trim_line(strip_line_comment(raw));
        if (t.empty())
        {
            if (inBlock && braceDepth > 0)
            {
                continue;
            }
            continue;
        }

        if (!inBlock)
        {
            if (!is_material_declaration_start_line(t))
            {
                continue;
            }
            blockBuffer.assign(t.data(), t.size());
            braceDepth = net_brace_delta(t);
            inBlock = true;
            if (braceDepth <= 0)
            {
                const bool parsed = try_parse_material_line(blockBuffer, definitions);
                (void)parsed;
                blockBuffer.clear();
                inBlock = false;
                braceDepth = 0;
            }
            continue;
        }

        if (!blockBuffer.empty())
        {
            blockBuffer.push_back(' ');
        }
        blockBuffer.append(t.data(), t.size());
        braceDepth += net_brace_delta(t);
        if (braceDepth <= 0)
        {
            const bool parsed = try_parse_material_line(blockBuffer, definitions);
            (void)parsed;
            blockBuffer.clear();
            inBlock = false;
            braceDepth = 0;
        }
    }

    if (inBlock && braceDepth > 0)
    {
        material_parse_error(blockBuffer, "unclosed material declaration block");
    }
}

bool is_line_skipped_for_axiom(const std::vector<std::string_view>& lines, std::size_t index)
{
    if (index >= lines.size())
    {
        return false;
    }

    std::size_t lineIndex = 0;
    while (lineIndex < lines.size())
    {
        const std::string_view t = trim_line(strip_line_comment(lines[lineIndex]));
        if (t.empty())
        {
            ++lineIndex;
            continue;
        }

        if (!is_material_declaration_start_line(t))
        {
            if (lineIndex == index)
            {
                return false;
            }
            ++lineIndex;
            continue;
        }

        const std::size_t blockStart = lineIndex;
        int braceDepth = net_brace_delta(t);
        ++lineIndex;
        while (braceDepth > 0 && lineIndex < lines.size())
        {
            const std::string_view cont = trim_line(strip_line_comment(lines[lineIndex]));
            if (!cont.empty())
            {
                braceDepth += net_brace_delta(cont);
            }
            ++lineIndex;
        }

        if (index >= blockStart && index < lineIndex)
        {
            return true;
        }
    }

    return false;
}
