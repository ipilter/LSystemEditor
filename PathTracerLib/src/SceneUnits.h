#pragma once

// World space uses millimeters: 1 scene unit = 1 mm.
// L-system turtle distances (F/f) and branch radii are also in millimeters.

#if defined(__CUDACC__)
#define SCENE_UNITS_FN __host__ __device__ inline
#else
#define SCENE_UNITS_FN inline
#endif

namespace SceneUnits {

static constexpr float kMillimetersPerUnit = 1.0f;
static constexpr float kUnitsPerMillimeter = 1.0f;

// Linear thrust: acceleration in mm/s² when input is held at full strength.
// Linear drag: velocity damping in 1/s (not mm/s²) — same unit as angular drag.
static constexpr float kDefaultLinearThrustMmPerSec2 = 2000.0f;
static constexpr float kDefaultLinearDragPerSec = 4.0f;
static constexpr float kLegacyLinearThrustScaleThreshold = 100.0f;
static constexpr float kLegacyMisscaledLinearDragThreshold = 100.0f;

// World-space length of each origin gizmo axis (1 m).
static constexpr float kOriginGizmoAxisLengthMm = 1000.0f;

// Focus-point gizmo billboard size (10 mm square).
static constexpr float kFocusGizmoSizeMm = 10.0f;

static constexpr float millimetersToUnits(float mm)
{
    return mm * kUnitsPerMillimeter;
}

static constexpr float unitsToMillimeters(float units)
{
    return units * kMillimetersPerUnit;
}

// Ray offset / tMin epsilon in millimeters (world units).
static constexpr float kMinRayEpsilonMm = 0.01f;
static constexpr float kRelativeRayEpsilon = 1.0e-3f;
static constexpr float kSceneExtentRayEpsilonScale = 1.0e-6f;

// Maximum ray parameter for visibility, shadow, and path tracing (1 km in mm).
static constexpr float kDefaultRayTMaxMm = 1'000'000.0f;

// Vertex normal welding grid: max(extent * scale, min cell) in mm.
static constexpr float kNormalWeldExtentScale = 1.0e-6f;
static constexpr float kMinNormalWeldCellMm = kMinRayEpsilonMm;

// Manifold RefineToTolerance geometric deviation setpoint (mm).
static constexpr float kDefaultGlobalRefineToleranceMm = 0.01f;
static constexpr float kDefaultSegmentRefineToleranceMm = 0.01f;

SCENE_UNITS_FN float normalWeldCellSizeMm(float meshExtentMm)
{
    const float extentBased = kNormalWeldExtentScale * meshExtentMm;
    return extentBased > kMinNormalWeldCellMm ? extentBased : kMinNormalWeldCellMm;
}

SCENE_UNITS_FN float rayEpsilonMm(float hitDistanceMm, float sceneExtentMm)
{
    float epsilon = kMinRayEpsilonMm;
    const float relative = kRelativeRayEpsilon * hitDistanceMm;
    if (relative > epsilon) {
        epsilon = relative;
    }
    const float extentBased = kSceneExtentRayEpsilonScale * sceneExtentMm;
    if (extentBased > epsilon) {
        epsilon = extentBased;
    }
    return epsilon;
}

#undef SCENE_UNITS_FN

} // namespace SceneUnits
