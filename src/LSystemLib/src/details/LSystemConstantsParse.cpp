#include "LSystemConstantsParse.h"

#include "LSystemMaterialParse.h"

#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace
{

[[noreturn]] void constant_parse_error(std::string_view line, const char* message)
{
    throw std::runtime_error(std::string(message) + ": " + std::string(line));
}

bool parse_ident(std::string_view line, std::size_t& pos, std::string& out)
{
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
    {
        ++pos;
    }
    if (pos >= line.size())
    {
        return false;
    }
    const char c0 = line[pos];
    if (!(std::isalpha(static_cast<unsigned char>(c0)) || c0 == '_'))
    {
        return false;
    }
    const std::size_t start = pos;
    ++pos;
    while (pos < line.size())
    {
        const char c = line[pos];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
        {
            break;
        }
        ++pos;
    }
    out.assign(line.data() + start, pos - start);
    return true;
}

bool parse_number(std::string_view line, std::size_t& pos, double& out)
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
    out = std::strtod(begin, &end);
    if (end == begin)
    {
        return false;
    }
    pos = static_cast<std::size_t>(end - line.data());
    return true;
}

} // namespace

bool is_global_constant_line(std::string_view trimmed_line)
{
    if (trimmed_line.empty() || is_material_declaration_line(trimmed_line))
    {
        return false;
    }
    std::size_t pos = 0;
    std::string name;
    if (!parse_ident(trimmed_line, pos, name))
    {
        return false;
    }
    while (pos < trimmed_line.size() && std::isspace(static_cast<unsigned char>(trimmed_line[pos])))
    {
        ++pos;
    }
    if (pos >= trimmed_line.size() || trimmed_line[pos] != '=')
    {
        return false;
    }
    ++pos;
    double v = 0.0;
    if (!parse_number(trimmed_line, pos, v))
    {
        return false;
    }
    (void)v;
    while (pos < trimmed_line.size() && std::isspace(static_cast<unsigned char>(trimmed_line[pos])))
    {
        ++pos;
    }
    return pos == trimmed_line.size();
}

bool try_parse_constant_line(std::string_view trimmed_line, std::unordered_map<std::string, double>& constants)
{
    if (!is_global_constant_line(trimmed_line))
    {
        return false;
    }

    std::size_t pos = 0;
    std::string name;
    if (!parse_ident(trimmed_line, pos, name))
    {
        return false;
    }
    while (pos < trimmed_line.size() && std::isspace(static_cast<unsigned char>(trimmed_line[pos])))
    {
        ++pos;
    }
    if (trimmed_line[pos] != '=')
    {
        constant_parse_error(trimmed_line, "constant declaration expected '='");
    }
    ++pos;
    double v = 0.0;
    if (!parse_number(trimmed_line, pos, v))
    {
        constant_parse_error(trimmed_line, "constant declaration expected number");
    }
    while (pos < trimmed_line.size() && std::isspace(static_cast<unsigned char>(trimmed_line[pos])))
    {
        ++pos;
    }
    if (pos != trimmed_line.size())
    {
        constant_parse_error(trimmed_line, "unexpected text after constant declaration");
    }

    if (constants.find(name) != constants.end())
    {
        constant_parse_error(trimmed_line, "duplicate constant name");
    }
    constants.emplace(std::move(name), v);
    return true;
}

void parse_constants_from_lines(const std::vector<std::string_view>& lines,
    std::unordered_map<std::string, double>& constants)
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
        if (is_global_constant_line(t))
        {
            const bool parsed = try_parse_constant_line(t, constants);
            (void)parsed;
        }
    }
}
