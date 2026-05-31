#pragma once

#include <vector_types.h>

struct CameraGpu
{
    float3 position;
    float4 orientation;
    float fovY;
    float aspect;
    float nearPlane;
    float farPlane;
};
