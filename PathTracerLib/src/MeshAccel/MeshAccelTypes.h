#pragma once

#include "Geometry/GeometryTypes.h"

#include <cstddef>
#include <cstdint>

#if defined(__CUDACC__)
#include <cuda_runtime.h>
#else
#include <cuda_runtime.h>
#endif

enum class TextureKind : uint32_t
{
    ConstantScalar = 0,
    ConstantRgb = 1,
    Grid2D = 2,
    Stripe1D = 3,
    Noise2D = 4,
};

struct TextureDescGpu
{
    uint32_t kind = 0;
    float4 p0 = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 p1 = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 p2 = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
};

struct TextureEvalContext
{
    Vec2 uv{};
    float u1d = 0.0f;
};

enum class RenderViewOverlayMode : int
{
    Render = 0,
    Bvh = 1,
    AdaptiveSampling = 2,
    Uv = 3,
    Normals = 4,
};

enum MeshBvhNodeFlags : uint32_t
{
    MeshBvhFlagLeaf = 1u << 0,
};

enum class MaterialType : uint32_t
{
    Opaque = 0,
    Glass = 1,
    Subsurface = 2,
    Emissive = 3,
};

struct alignas(16) MaterialGpu
{
    float r = 0.8f;
    float g = 0.8f;
    float b = 0.8f;
    float roughness = 0.5f;
    float metallic = 0.0f;
    /**
     * @brief Emission intensity multiplier; radiance = base_color * emission.
     * Scene-unit radiance scale (1.0 ≈ moderate area light).
     */
    float emission = 0.0f;
    /** @brief Oren-Nayar diffuse roughness in [0, 1]; negative = use roughness. */
    float diffuseRoughness = -1.0f;
    /** @brief Dielectric F0 multiplier in [0, 1]; 1 = physical IOR-derived F0. */
    float specular = 1.0f;
    /** @brief Volume absorption coefficient per channel in 1/mm. */
    float sigmaAr = 0.0f;
    float sigmaAg = 0.0f;
    float sigmaAb = 0.0f;
    /** @brief Volume scattering coefficient per channel in 1/mm. */
    float sigmaSr = 0.0f;
    float sigmaSg = 0.0f;
    float sigmaSb = 0.0f;
    /** @brief Henyey-Greenstein anisotropy in [-1, 1]. */
    float mediumG = 0.0f;
    float ior = 1.5f;
    /** @brief Abbe number for IOR dispersion; crown-glass default. */
    float abbeNumber = 58.0f;
    /** @brief MaterialType value; 0 = Opaque when unset. */
    uint32_t materialType = 0;
    /** @brief Subsurface scattering weight in [0, 1]. */
    float subsurface = 0.0f;
    /** @brief Subsurface diffusion radius per channel in mm. */
    float subsurfaceRadiusR = 1.0f;
    float subsurfaceRadiusG = 1.0f;
    float subsurfaceRadiusB = 1.0f;
    /** @brief Scales subsurfaceRadius for geometry tuning; default 1. */
    float subsurfaceScatterScale = 1.0f;

    /** @brief 0 = use inline field; else index into scene texture bank. */
    uint32_t albedoTex = 0;
    uint32_t roughnessTex = 0;
    uint32_t metallicTex = 0;
    uint32_t emissionTex = 0;
    uint32_t diffuseRoughnessTex = 0;
    uint32_t specularTex = 0;
    uint32_t sigmaATex = 0;
    uint32_t sigmaSTex = 0;
    uint32_t mediumGTex = 0;
    uint32_t iorTex = 0;
};

static_assert(sizeof(MaterialGpu) % alignof(MaterialGpu) == 0);

struct alignas(16) TriangleGpu
{
    Vec3 v0{};
    Vec3 v1{};
    Vec3 v2{};
    Vec3 n0{};
    Vec3 n1{};
    Vec3 n2{};
    Vec2 uv0{};
    Vec2 uv1{};
    Vec2 uv2{};
    uint32_t materialIndex = 0;
};

static_assert(sizeof(TriangleGpu) % alignof(TriangleGpu) == 0);

struct alignas(16) MeshBvhNode
{
    Vec3 boundsMin{};

    float _pad0 = 0.0f;

    Vec3 boundsMax{};

    float _pad1 = 0.0f;

    uint32_t leftIndex = 0;
    uint32_t rightIndex = 0;
    uint32_t triStart = 0;
    uint32_t triCount = 0;
};

static_assert(sizeof(MeshBvhNode) == 48);
static_assert(offsetof(MeshBvhNode, leftIndex) == 32);

struct alignas(16) MeshAccelSceneGpu
{
    const MeshBvhNode* bvhNodes = nullptr;
    const TriangleGpu* triangles = nullptr;
    const MaterialGpu* materials = nullptr;
    const uint32_t* emissiveTriangleIndices = nullptr;
    /** @brief Prefix CDF over emissive triangles (length = emissiveTriangleCount + 1). */
    const float* emissiveTriangleCdf = nullptr;
    const TextureDescGpu* textures = nullptr;
    uint32_t bvhNodeCount = 0;
    uint32_t triangleCount = 0;
    uint32_t bvhRootIndex = 0;
    uint32_t materialCount = 0;
    uint32_t emissiveTriangleCount = 0;
    uint32_t textureCount = 0;
    /** @brief Longest AABB axis length in scene units (mm); used for scale-aware ray epsilon. */
    float sceneExtentMm = 0.0f;
};

static_assert(sizeof(MeshAccelSceneGpu) % alignof(MeshAccelSceneGpu) == 0);

struct MeshHit
{
    bool hit = false;
    float t = 0.0f;
    Vec3 normal{};
    Vec2 uv{};
    uint32_t triangleIndex = 0;
};
