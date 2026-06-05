#pragma once

#include "LSystemMaterials.h"
#include "LSystemModel.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class LSystemImpl;

class LSystem
{
public:
    LSystem();
    ~LSystem();

    LSystem(const LSystem&) = delete;
    LSystem& operator=(const LSystem&) = delete;

    LSystem(LSystem&&) noexcept;
    LSystem& operator=(LSystem&&) noexcept;

    void parse(const std::string& str);

    const std::vector<Symbol>& axiom_modules() const;
    const std::vector<ParsedRule>& rules() const;
    const std::vector<MaterialDefinition>& material_definitions() const;
    const std::unordered_map<std::string, double>& constants() const;

private:
    std::unique_ptr<LSystemImpl> m_impl;
};
