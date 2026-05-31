#pragma once

#include "CameraGpu.h"

#include <cmath>
#include <vector_types.h>

__device__ inline float3 rotateByQuat(float4 q, float3 v)
{
    const float3 u = make_float3(q.x, q.y, q.z);
    const float s = q.w;
    const float3 cross1 = make_float3(
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x);
    const float3 cross2 = make_float3(
        u.y * cross1.z - u.z * cross1.y,
        u.z * cross1.x - u.x * cross1.z,
        u.x * cross1.y - u.y * cross1.x);
    return make_float3(
        v.x + 2.0f * cross1.x * s + 2.0f * cross2.x,
        v.y + 2.0f * cross1.y * s + 2.0f * cross2.y,
        v.z + 2.0f * cross1.z * s + 2.0f * cross2.z);
}

__device__ inline float3 normalize3(float3 v)
{
    const float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.0f) {
        return make_float3(0.0f, 0.0f, -1.0f);
    }
    const float invLen = 1.0f / len;
    return make_float3(v.x * invLen, v.y * invLen, v.z * invLen);
}

__device__ inline float3 cameraRayDirection(const CameraGpu* camera, float u, float v)
{
    const float ndcX = 2.0f * u - 1.0f;
    const float ndcY = 1.0f - 2.0f * v;

    const float tanHalf = tanf(camera->fovY * 0.5f);
    const float top = camera->nearPlane * tanHalf;
    const float right = top * camera->aspect;

    const float3 viewDir = make_float3(ndcX * right, ndcY * top, -camera->nearPlane);
    return normalize3(rotateByQuat(camera->orientation, viewDir));
}

__device__ inline void cameraPrimaryRay(const CameraGpu* camera, float u, float v, float3& ro, float3& rd)
{
    ro = camera->position;
    rd = cameraRayDirection(camera, u, v);
}
