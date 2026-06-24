#include "LSystemImpl.h"

#include "LSystemConstantsParse.h"
#include "LSystemMaterialParse.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace
{

std::string_view trim_in_place(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    {
        s.remove_suffix(1);
    }
    return s;
}

std::string_view strip_hash_comment(std::string_view raw)
{
    const std::size_t hash = raw.find('#');
    if (hash == std::string_view::npos)
    {
        return raw;
    }
    return raw.substr(0, hash);
}

void split_raw_lines(const std::string& str, std::vector<std::string_view>& out)
{
    out.clear();
    std::size_t i = 0;
    while (i < str.size())
    {
        const std::size_t nl = str.find('\n', i);
        const std::string_view raw =
            (nl == std::string::npos) ? std::string_view(str).substr(i) : std::string_view(str).substr(i, nl - i);
        out.push_back(raw);
        if (nl == std::string::npos)
        {
            break;
        }
        i = nl + 1;
    }
}

} // namespace

LSystemImpl::LSystemImpl() = default;

LSystemImpl::~LSystemImpl() = default;

void LSystemImpl::parse(const std::string& str)
{
    std::vector<std::string_view> lines;
    split_raw_lines(str, lines);

    m_material_definitions.clear();
    parse_materials_from_lines(lines, m_material_definitions);

    m_constants.clear();
    parse_constants_from_lines(lines, m_constants);

    m_rules.clear();
    if (lines.empty())
    {
        m_axiom.clear();
        return;
    }

    std::size_t first_rule_line = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view t = trim_in_place(strip_hash_comment(lines[i]));
        if (t.find("->") != std::string_view::npos)
        {
            first_rule_line = i;
            break;
        }
    }

    std::string axiom_text;
    axiom_text.reserve(str.size());
    bool axiom_need_space = false;
    for (std::size_t i = 0; i < first_rule_line; ++i)
    {
        const std::string_view t = trim_in_place(strip_hash_comment(lines[i]));
        if (t.empty() || is_material_declaration_start_line(t)
            || is_line_skipped_for_axiom(lines, i) || is_global_constant_line(t))
        {
            continue;
        }
        if (axiom_need_space)
        {
            axiom_text.push_back(' ');
        }
        axiom_need_space = true;
        axiom_text.append(t.data(), t.size());
    }
    m_axiom = ModuleStringParser::parse(axiom_text);

    std::string rules_text;
    rules_text.reserve(str.size());
    for (std::size_t k = first_rule_line; k < lines.size(); ++k)
    {
        if (k > first_rule_line)
        {
            rules_text.push_back('\n');
        }
        rules_text.append(lines[k].data(), lines[k].size());
    }
    m_rules = ModuleStringParser::parse_rules_block(rules_text);
}
