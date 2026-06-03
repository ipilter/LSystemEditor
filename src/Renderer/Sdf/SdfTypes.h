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
    float exactSwitchThreshold = 0.1f;
    int maxSteps = 256;
    int refineIterations = 10;
    int enableRaySort = 1;
    float backgroundR = 10.0f / 255.0f;
    float backgroundG = 10.0f / 255.0f;
    float backgroundB = 10.0f / 255.0f;
};

using SdfMarchParamsHost = SdfMarchParamsGpu;

struct SdfHit
{
    bool hit = false;
    float t = 0.0f;
    int steps = 0;
    float sdfAtHit = 0.0f;
    SdfFloat3 normal{};
};

enum class SdfDebugVisualMode : int
{
    Off = 0,
    StepCount = 1,
    HitDistance = 2,
};

enum class SdfTraversalMode : int
{
    BruteForce = 0,
    BvhAccel = 1,
};

enum class SdfAccelBoundsOverlayMode : int
{
    Off = 0,
    Bvh = 1,
};
