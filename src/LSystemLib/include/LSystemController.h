#pragma once

#include "LSystem.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

class LSystemController
{
public:
    void register_system(std::string name, LSystem system);
    const LSystem* find(std::string_view name) const;
    void validate(const LSystem& root) const;

private:
    void validate_impl(const LSystem& sys, std::unordered_set<std::string>& visiting) const;

    std::unordered_map<std::string, LSystem> m_systems;
};
