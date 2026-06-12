#pragma once

#include "LSystemMaterials.h"
#include "LSystemModel.h"
#include "ModuleStringParser.h"

#include <string>
#include <unordered_map>
#include <vector>

class LSystemImpl
{
public:
    LSystemImpl();
    ~LSystemImpl();

    void parse(const std::string& str);

    const std::vector<Symbol>& axiom_modules() const { return m_axiom; }
    const std::vector<ParsedRule>& rules() const { return m_rules; }
    const std::vector<MaterialDefinition>& material_definitions() const { return m_material_definitions; }
    const std::unordered_map<std::string, double>& constants() const { return m_constants; }

private:
    std::vector<Symbol> m_axiom;
    std::vector<ParsedRule> m_rules;
    std::vector<MaterialDefinition> m_material_definitions;
    std::unordered_map<std::string, double> m_constants;
};
