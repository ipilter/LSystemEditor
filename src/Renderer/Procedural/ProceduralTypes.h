#pragma once

#include "Geometry/GeometryTypes.h"

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
    float defaultStepLength = 0.5f;
    float defaultRadius = 0.1f;
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
    float segmentRefineTolerance = 0.01f;
    float globalRefineTolerance = 0.01f;
    TurtleParams turtle{};
};

struct ProceduralInstance
{
    std::string commandString;
    std::size_t iterations = 0;
    Vec3 translation{};
    Vec3 rotationDeg{};
};
