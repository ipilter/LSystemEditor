#pragma once

#include "Geometry/GeometryTypes.h"
#include "SceneUnits.h"

#include <cstddef>
#include <string>
#include <vector>

struct TurtleState
{
    Vec3 position{};
    Vec3 tangent{};
    float radius = 0.1f;
};

struct TurtleSegment
{
    std::vector<TurtleState> states;
    std::string materialId = "0";
};

struct BranchGroup
{
    std::vector<size_t> segmentIndices;
};

struct TurtleOutput
{
    std::vector<TurtleSegment> segments;
    std::vector<BranchGroup> branchGroups;
};

struct TurtleParams
{
    /** @brief Default turtle step length in mm. */
    float defaultStepLength = 500.0f;
    /** @brief Default branch radius in mm. */
    float defaultRadius = 100.0f;
};

struct RootTransform
{
    Vec3 translation{};
    Vec3 rotationDeg{};
};

struct SplineSample
{
    Vec3 position{};
    Vec3 tangent{};
    float radius = 0.1f;
};

struct PathFrame
{
    Vec3 position{};
    Vec3 tangent{};
    Vec3 normal{};
    Vec3 binormal{};
    float radius = 0.1f;
};

struct ProceduralBuildParams
{
    int circularSegments = 32;
    int samplesPerSpan = 4;
    float hermiteTension = 1.0f;
    /** @brief Manifold geometric deviation tolerance in mm; see SceneUnits::kDefaultSegmentRefineToleranceMm. */
    float segmentRefineTolerance = SceneUnits::kDefaultSegmentRefineToleranceMm;
    /** @brief Manifold RefineToTolerance setpoint in mm; see SceneUnits::kDefaultGlobalRefineToleranceMm. */
    float globalRefineTolerance = SceneUnits::kDefaultGlobalRefineToleranceMm;
    float creaseAngleDeg = 50.0f;
    TurtleParams turtle{};
};

struct ProceduralInstance
{
    std::string commandString;
    std::size_t iterations = 0;
    Vec3 translation{};
    Vec3 rotationDeg{};
};
