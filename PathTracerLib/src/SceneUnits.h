#pragma once

// World space uses millimeters: 1 scene unit = 1 mm.
// L-system turtle distances (F/f) and branch radii are also in millimeters.
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

} // namespace SceneUnits
