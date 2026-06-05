#pragma once

#include "LSystemModel.h"

#include <string_view>
#include <vector>

class ModuleStringParser
{
public:
    static std::vector<Symbol> parse(std::string_view input);
    static ParsedRule parse_rule(std::string_view input);
    /// Physical lines merged using leading space/tab continuation; blank lines separate rules only
    /// when no rule is being accumulated. Full-line `#` and blank lines inside a continued rule are skipped.
    /// Other lines are truncated at the first `#` (inline to-end-of-line comment) before parsing.
    static std::vector<ParsedRule> parse_rules_block(std::string_view rules_text);
};
