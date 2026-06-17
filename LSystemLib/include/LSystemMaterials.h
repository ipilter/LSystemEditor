#pragma once

#include <string>

/** @brief Parsed material properties from `Mat(id) = { ... }` lines. */
struct MaterialEntry
{
    float r = 0.8f;
    float g = 0.8f;
    float b = 0.8f;
    float roughness = 0.5f;
    float metallic = 0.f;
    /** @brief Emissive strength multiplier on base color (0 = non-emissive). */
    float emission = 0.f;
    float ior = 1.5f;
    /** @brief Transmission in [0, 1]; 0 = opaque, 1 = fully transmissive. */
    float transmission = 0.0f;
    /** @brief Thin shell in [0, 1]; 0 = thick, 1 = thin translucency. */
    float thin = 0.0f;
    /** @brief Subsurface influence in [0, 1]. */
    float subsurface = 0.0f;
};

/** @brief One material definition collected during L-system parse. */
struct MaterialDefinition
{
    /** @brief Material name or numeric id as string (e.g. `"Glass"`, `"0"`). */
    std::string id;
    MaterialEntry entry;
};
