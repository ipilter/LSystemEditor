#include "LSystemMaterialParse.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdint>
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

float clamp_ior(float v)
{
    return std::clamp(v, 1.0f, 3.0f);
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

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
        {
            return false;
        }
    }
    return true;
}

bool parse_identifier(std::string_view line, std::size_t& pos, std::string& out)
{
    skip_ws(line, pos);
    if (pos >= line.size() || !std::isalpha(static_cast<unsigned char>(line[pos])))
    {
        return false;
    }
    const std::size_t begin = pos;
    while (pos < line.size() &&
           (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '_'))
    {
        ++pos;
    }
    out.assign(line.substr(begin, pos - begin));
    return true;
}

bool resolve_material_kind(std::string_view name, MaterialKind& out_kind)
{
    if (iequals(name, "Diffuse"))
    {
        out_kind = MaterialKind::Diffuse;
        return true;
    }
    if (iequals(name, "Metal"))
    {
        out_kind = MaterialKind::Metal;
        return true;
    }
    if (iequals(name, "Glass") || iequals(name, "Transparent"))
    {
        out_kind = MaterialKind::Glass;
        return true;
    }
    return false;
}

void assign_rgb(MaterialEntry& entry, const std::vector<float>& comps)
{
    entry.r = clamp01(comps[0]);
    entry.g = clamp01(comps[1]);
    entry.b = clamp01(comps[2]);
}

void assign_diffuse(MaterialEntry& entry, const std::vector<float>& comps)
{
    assign_rgb(entry, comps);
    entry.kind = MaterialKind::Diffuse;
    entry.metallic = 0.f;
    entry.transmission = 0.f;
    entry.roughness = comps.size() >= 4u ? clamp01(comps[3]) : 0.5f;
    entry.emission = comps.size() >= 5u ? std::max(0.f, comps[4]) : 0.f;
}

void assign_metal(MaterialEntry& entry, const std::vector<float>& comps)
{
    assign_rgb(entry, comps);
    entry.kind = MaterialKind::Metal;
    entry.metallic = 1.f;
    entry.transmission = 0.f;
    entry.roughness = comps.size() >= 4u ? clamp01(comps[3]) : 0.5f;
    entry.emission = comps.size() >= 5u ? std::max(0.f, comps[4]) : 0.f;
}

void assign_glass(MaterialEntry& entry, const std::vector<float>& comps)
{
    assign_rgb(entry, comps);
    entry.kind = MaterialKind::Glass;
    entry.metallic = 0.f;
    entry.transmission = 1.f;
    entry.ior = clamp_ior(comps[3]);
    entry.roughness = comps.size() >= 5u ? clamp01(comps[4]) : 0.f;
    entry.emission = comps.size() >= 6u ? std::max(0.f, comps[5]) : 0.f;
}

void validate_component_count(std::string_view line, MaterialKind kind, std::size_t count)
{
    switch (kind)
    {
    case MaterialKind::Diffuse:
        if (count < 3u || count > 5u)
        {
            material_parse_error(line, "Diffuse requires 3 to 5 components (r, g, b, [roughness], [emission])");
        }
        return;
    case MaterialKind::Metal:
        if (count < 3u || count > 5u)
        {
            material_parse_error(line, "Metal requires 3 to 5 components (r, g, b, [roughness], [emission])");
        }
        return;
    case MaterialKind::Glass:
        if (count < 4u || count > 6u)
        {
            material_parse_error(line, "Glass requires 4 to 6 components (r, g, b, ior, [roughness], [emission])");
        }
        return;
    }
}

void assign_entry_from_kind(MaterialEntry& entry, MaterialKind kind, const std::vector<float>& comps)
{
    switch (kind)
    {
    case MaterialKind::Diffuse:
        assign_diffuse(entry, comps);
        return;
    case MaterialKind::Metal:
        assign_metal(entry, comps);
        return;
    case MaterialKind::Glass:
        assign_glass(entry, comps);
        return;
    }
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
    if (pos >= trimmed_line.size() || trimmed_line[pos] != '=')
    {
        return false;
    }
    ++pos;
    std::string type_name;
    if (!parse_identifier(trimmed_line, pos, type_name))
    {
        return false;
    }
    skip_ws(trimmed_line, pos);
    return pos < trimmed_line.size() && trimmed_line[pos] == '{';
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

    std::string type_name;
    if (!parse_identifier(trimmed_line, pos, type_name))
    {
        material_parse_error(trimmed_line, "material declaration requires type keyword (Diffuse, Metal, Glass)");
    }

    MaterialKind kind{};
    if (!resolve_material_kind(type_name, kind))
    {
        material_parse_error(trimmed_line, "unknown material type");
    }

    std::vector<float> comps;
    if (!parse_brace_float_list(trimmed_line, pos, comps))
    {
        material_parse_error(trimmed_line, "material declaration expected '{'");
    }

    skip_ws(trimmed_line, pos);
    if (pos != trimmed_line.size())
    {
        material_parse_error(trimmed_line, "unexpected text after material declaration");
    }

    validate_component_count(trimmed_line, kind, comps.size());

    if (material_id_defined(definitions, id))
    {
        material_parse_error(trimmed_line, "duplicate material id");
    }

    MaterialDefinition def;
    def.id = id;
    assign_entry_from_kind(def.entry, kind, comps);
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
