#pragma once

struct SdfFloat3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct SdfFloat2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct SdfMarchParamsGpu
{
    float maxDistance = 100.0f;
    float surfaceEpsilon = 1.0e-4f;
    float normalEpsilon = 1.0e-4f;
    int maxSteps = 256;
    int refineIterations = 10;
};

using SdfMarchParamsHost = SdfMarchParamsGpu;

struct SdfSceneGpu
{
    SdfFloat3 cylinderCenter{};
    float _pad0 = 0.0f;
    SdfFloat2 cylinderHalfExtents{};
    SdfFloat2 _pad1{};

    SdfFloat3 sphereCenter{};
    float sphereRadius = 0.0f;

    SdfFloat3 coneCenter{};
    float coneHalfHeight = 0.0f;
    float coneRadiusBottom = 0.0f;
    float coneRadiusTop = 0.0f;
    float _pad2 = 0.0f;
};

struct SdfHit
{
    bool hit = false;
    float t = 0.0f;
    int steps = 0;
    float sdfAtHit = 0.0f;
    SdfFloat3 normal{};
};

enum class SdfVisualMode : int
{
    StepCount = 0,
    HitDistance = 1,
    Normals = 2,
};
