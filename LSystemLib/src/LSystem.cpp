#include "LSystem.h"

#include "LSystemImpl.h"

LSystem::LSystem()
    : m_impl(std::make_unique<LSystemImpl>())
{
}

LSystem::~LSystem() = default;

LSystem::LSystem(LSystem&&) noexcept = default;

LSystem& LSystem::operator=(LSystem&&) noexcept = default;

void LSystem::parse(const std::string& str)
{
    m_impl->parse(str);
}

const std::vector<Symbol>& LSystem::axiom_modules() const
{
    return m_impl->axiom_modules();
}

const std::vector<ParsedRule>& LSystem::rules() const
{
    return m_impl->rules();
}

const std::vector<MaterialDefinition>& LSystem::material_definitions() const
{
    return m_impl->material_definitions();
}

const std::unordered_map<std::string, double>& LSystem::constants() const
{
    return m_impl->constants();
}
