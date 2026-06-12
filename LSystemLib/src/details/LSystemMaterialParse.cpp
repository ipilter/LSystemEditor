#include "LSystemMaterialParse.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <string>

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

bool parse_mat_id_paren(std::string_view line, std::size_t& pos, std::uint32_t& out_id)
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
    skip_ws(line, pos);
    if (pos >= line.size() || !std::isdigit(static_cast<unsigned char>(line[pos])))
    {
        return false;
    }
    std::uint64_t acc = 0;
    while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])))
    {
        acc = acc * 10u + static_cast<unsigned>(line[pos] - '0');
        ++pos;
        if (acc > UINT32_MAX)
        {
            return false;
        }
    }
    skip_ws(line, pos);
    if (pos >= line.size() || line[pos] != ')')
    {
        return false;
    }
    ++pos;
    out_id = static_cast<std::uint32_t>(acc);
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

} // namespace

bool is_material_declaration_line(std::string_view trimmed_line)
{
    std::size_t pos = 0;
    std::uint32_t id = 0;
    if (!parse_mat_id_paren(trimmed_line, pos, id))
    {
        return false;
    }
    skip_ws(trimmed_line, pos);
    return pos < trimmed_line.size() && trimmed_line[pos] == '=';
}

bool material_id_defined(const std::vector<MaterialDefinition>& definitions, const std::uint32_t id)
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
    std::uint32_t id = 0;
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
    while (pos < trimmed_line.size() && std::isspace(static_cast<unsigned char>(trimmed_line[pos])))
    {
        ++pos;
    }
    if (pos >= trimmed_line.size() || trimmed_line[pos] != '{')
    {
        material_parse_error(trimmed_line, "material declaration expected '{'");
    }
    ++pos;

    std::vector<float> comps;
    comps.reserve(5);
    for (;;)
    {
        while (pos < trimmed_line.size() && std::isspace(static_cast<unsigned char>(trimmed_line[pos])))
        {
            ++pos;
        }
        if (pos < trimmed_line.size() && trimmed_line[pos] == '}')
        {
            ++pos;
            break;
        }
        float v = 0.f;
        if (!parse_float(trimmed_line, pos, v))
        {
            material_parse_error(trimmed_line, "material declaration expected number");
        }
        comps.push_back(v);
        while (pos < trimmed_line.size() && std::isspace(static_cast<unsigned char>(trimmed_line[pos])))
        {
            ++pos;
        }
        if (pos < trimmed_line.size() && trimmed_line[pos] == ',')
        {
            ++pos;
            continue;
        }
        if (pos < trimmed_line.size() && trimmed_line[pos] == '}')
        {
            ++pos;
            break;
        }
        material_parse_error(trimmed_line, "material declaration expected ',' or '}'");
    }

    while (pos < trimmed_line.size() && std::isspace(static_cast<unsigned char>(trimmed_line[pos])))
    {
        ++pos;
    }
    if (pos != trimmed_line.size())
    {
        material_parse_error(trimmed_line, "unexpected text after material declaration");
    }

    if (comps.size() < 3u || comps.size() > 6u)
    {
        material_parse_error(trimmed_line, "material declaration requires 3 to 6 components");
    }

    if (material_id_defined(definitions, id))
    {
        material_parse_error(trimmed_line, "duplicate material id");
    }

    MaterialDefinition def;
    def.id = id;
    def.entry.r = clamp01(comps[0]);
    def.entry.g = clamp01(comps[1]);
    def.entry.b = clamp01(comps[2]);
    if (comps.size() >= 4u)
    {
        def.entry.roughness = clamp01(comps[3]);
    }
    if (comps.size() >= 5u)
    {
        def.entry.metallic = clamp01(comps[4]);
    }
    if (comps.size() >= 6u)
    {
        def.entry.emission = std::max(0.f, comps[5]);
    }
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
