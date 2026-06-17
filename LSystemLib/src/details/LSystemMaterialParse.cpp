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
            material_parse_error(line, "material declaration expected number");
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
        material_parse_error(line, "material declaration expected ',' or '}'");
    }
}

float component_or_default(const std::vector<float>& comps, std::size_t index, float default_value)
{
    return index < comps.size() ? comps[index] : default_value;
}

void validate_parametric_component_count(std::string_view line, std::size_t count)
{
    if (count < 3u || count > 10u)
    {
        material_parse_error(
            line,
            "material requires 3 to 10 components "
            "(r, g, b, [roughness], [metallic], [transmission], [thin], [ior], [subsurface], [emission])");
    }
}

void assign_parametric_entry(MaterialEntry& entry, const std::vector<float>& comps)
{
    entry.r = clamp01(comps[0]);
    entry.g = clamp01(comps[1]);
    entry.b = clamp01(comps[2]);
    entry.roughness = clamp01(component_or_default(comps, 3u, 0.5f));
    entry.metallic = clamp01(component_or_default(comps, 4u, 0.f));
    entry.transmission = clamp01(component_or_default(comps, 5u, 0.f));
    entry.thin = clamp01(component_or_default(comps, 6u, 0.f));
    entry.ior = std::max(1.0e-3f, component_or_default(comps, 7u, 1.5f));
    entry.subsurface = clamp01(component_or_default(comps, 8u, 0.f));
    entry.emission = std::max(0.f, component_or_default(comps, 9u, 0.f));
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

    std::vector<float> comps;
    if (!parse_brace_float_list(trimmed_line, pos, comps))
    {
        material_parse_error(
            trimmed_line,
            "material declaration expected '{' "
            "(r, g, b, [roughness], [metallic], [transmission], [thin], [ior], [subsurface], [emission])");
    }

    skip_ws(trimmed_line, pos);
    if (pos != trimmed_line.size())
    {
        material_parse_error(trimmed_line, "unexpected text after material declaration");
    }

    validate_parametric_component_count(trimmed_line, comps.size());

    if (material_id_defined(definitions, id))
    {
        material_parse_error(trimmed_line, "duplicate material id");
    }

    MaterialDefinition def;
    def.id = id;
    assign_parametric_entry(def.entry, comps);
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
