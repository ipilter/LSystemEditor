#pragma once

#include <cstdint>

/** @brief Parsed material properties from `Mat(id) = { r, g, b, ... }` lines. */
struct MaterialEntry
{
    float r = 0.8f;
    float g = 0.8f;
    float b = 0.8f;
    float roughness = 0.5f;
    float metallic = 0.f;
    /** @brief Emissive strength multiplier on base color (0 = non-emissive). */
    float emission = 0.f;
    float a = 1.0f;
};

/** @brief One material definition collected during L-system parse. */
struct MaterialDefinition
{
    std::uint32_t id = 0;
    MaterialEntry entry;
};
