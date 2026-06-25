#pragma once

#include <cstdint>

struct EnvironmentMapGpu
{
    const float* pixels = nullptr;
    const float* marginalCdf = nullptr;
    const float* rowCdf = nullptr;
    int width = 0;
    int height = 0;
    int valid = 0;
};

struct RenderParamsGpu
{
    float backgroundR = 10.0f / 255.0f;
    float backgroundG = 10.0f / 255.0f;
    float backgroundB = 10.0f / 255.0f;
    float environmentIntensity = 1.0f;
    /** @brief Environment map yaw in degrees (0–359), rotation about world +Y. */
    int environmentRotationYDeg = 0;
    int maxPathDepth = 32;
    int russianRouletteMinDepth = 3;
    /** @brief Cap on interior HG scatter events per subsurface random walk (1–128). */
    int maxSubsurfaceScatters = 8;
    int minSamples = 16;
    int maxSamplesPerPixel = 1024;
    int debugOverlayMode = 0;
    /** @brief BrdfDebugFlags bit mask for glass/transmission debugging. */
    int brdfDebugFlags = 0;
    /** @brief Emissive triangle NEE samples per bounce (1–4). */
    int emissiveNeeSamples = 1;
    float relativeErrorThreshold = 0.02f;
};
