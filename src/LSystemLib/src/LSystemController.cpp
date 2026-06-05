#include "LSystemController.h"

#include "LSystemModel.h"

#include <stdexcept>
#include <string>

void LSystemController::register_system(std::string name, LSystem system)
{
    m_systems.insert_or_assign(std::move(name), std::move(system));
}

const LSystem* LSystemController::find(std::string_view name) const
{
    const auto it = m_systems.find(std::string(name));
    if (it == m_systems.end())
    {
        return nullptr;
    }
    return &it->second;
}

void LSystemController::validate_impl(const LSystem& sys, std::unordered_set<std::string>& visiting) const
{
    const auto on_symbol = [&](const Symbol& sym) {
        if (sym.kind != SymbolKind::SubsystemRef)
        {
            return;
        }
        const std::string& target = sym.subsystem;
        const auto it = m_systems.find(target);
        if (it == m_systems.end())
        {
            throw std::runtime_error("subsystem not found: " + target);
        }
        if (visiting.count(target) != 0)
        {
            return;
        }
        visiting.insert(target);
        validate_impl(it->second, visiting);
        visiting.erase(target);
    };

    for (const Symbol& sym : sys.axiom_modules())
    {
        on_symbol(sym);
    }
    for (const ParsedRule& rule : sys.rules())
    {
        for (const Symbol& sym : rule.successor)
        {
            on_symbol(sym);
        }
    }
}

void LSystemController::validate(const LSystem& root) const
{
    std::unordered_set<std::string> visiting;
    validate_impl(root, visiting);
}
