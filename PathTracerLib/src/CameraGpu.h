#pragma once

#include <vector_types.h>

struct CameraGpu
{
    float3 position;
    float4 orientation; // (w, x, y, z) stored in (.x, .y, .z, .w)
    float fovY;
    float aspect;
    float nearPlane;
    float farPlane;
    float apertureRadius = 0.0f;
    float focusDistance = 0.0f;
    float3 focusPoint{};
    int focusValid = 0;
};
