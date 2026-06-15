#pragma once

#include <cstdint>

/** @brief BRDF kind for `Mat(id) = Type { ... }` declarations. Values match PathTracer `MaterialKind`. */
enum class MaterialKind : std::uint8_t
{
    Diffuse = 0,
    Metal = 1,
    Glass = 2,
};

/** @brief Parsed material properties from `Mat(id) = Type { ... }` lines. */
struct MaterialEntry
{
    MaterialKind kind = MaterialKind::Diffuse;
    float r = 0.8f;
    float g = 0.8f;
    float b = 0.8f;
    float roughness = 0.5f;
    float metallic = 0.f;
    /** @brief Emissive strength multiplier on base color (0 = non-emissive). */
    float emission = 0.f;
    float ior = 1.5f;
    float transmission = 0.f;
};

/** @brief One material definition collected during L-system parse. */
struct MaterialDefinition
{
    std::uint32_t id = 0;
    MaterialEntry entry;
};
