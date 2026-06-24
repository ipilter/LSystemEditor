#pragma once

#include "CameraGpu.h"

#include <cmath>
#include <vector_types.h>

__device__ inline float3 rotateByQuat(float4 q, float3 v)
{
    // CameraGpu.orientation: (w, x, y, z) in (.x, .y, .z, .w)
    const float s = q.x;
    const float3 u = make_float3(q.y, q.z, q.w);
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

__device__ inline float2 sampleConcentricDisk(float u1, float u2)
{
    const float a = 2.0f * u1 - 1.0f;
    const float b = 2.0f * u2 - 1.0f;
    if (a == 0.0f && b == 0.0f) {
        return make_float2(0.0f, 0.0f);
    }

    float r = 0.0f;
    float theta = 0.0f;
    if (fabsf(a) > fabsf(b)) {
        r = a;
        theta = (3.14159265358979323846f * 0.25f) * (b / a);
    } else {
        r = b;
        theta = (3.14159265358979323846f * 0.5f) - (3.14159265358979323846f * 0.25f) * (a / b);
    }
    return make_float2(r * cosf(theta), r * sinf(theta));
}

__device__ inline void cameraBasis(const CameraGpu* camera, float3& forward, float3& right, float3& up)
{
    forward = normalize3(rotateByQuat(camera->orientation, make_float3(0.0f, 0.0f, -1.0f)));
    right = normalize3(rotateByQuat(camera->orientation, make_float3(1.0f, 0.0f, 0.0f)));
    up = normalize3(rotateByQuat(camera->orientation, make_float3(0.0f, 1.0f, 0.0f)));
}

__device__ inline float3 cameraFilmPointLocal(const CameraGpu* camera, float u, float v)
{
    const float ndcX = 2.0f * u - 1.0f;
    const float ndcY = 1.0f - 2.0f * v;

    const float tanHalf = tanf(camera->fovY * 0.5f);
    const float top = camera->nearPlane * tanHalf;
    const float rightExtent = top * camera->aspect;

    return make_float3(ndcX * rightExtent, ndcY * top, -camera->nearPlane);
}

__device__ inline float3 cameraRayDirection(const CameraGpu* camera, float u, float v)
{
    const float3 viewDir = cameraFilmPointLocal(camera, u, v);
    return normalize3(rotateByQuat(camera->orientation, viewDir));
}

__device__ inline void cameraPrimaryRay(const CameraGpu* camera, float u, float v, float3& ro, float3& rd)
{
    ro = camera->position;
    rd = cameraRayDirection(camera, u, v);
}

__device__ inline void cameraPrimaryRaySampled(
    const CameraGpu* camera,
    float u,
    float v,
    float lensU1,
    float lensU2,
    float3& ro,
    float3& rd)
{
    if (camera == nullptr) {
        ro = make_float3(0.0f, 0.0f, 0.0f);
        rd = make_float3(0.0f, 0.0f, -1.0f);
        return;
    }

    if (camera->focusValid == 0 || camera->apertureRadius <= 0.0f || camera->focusDistance <= 0.0f ||
        camera->nearPlane <= 0.0f) {
        cameraPrimaryRay(camera, u, v, ro, rd);
        return;
    }

    float3 forward{};
    float3 right{};
    float3 up{};
    cameraBasis(camera, forward, right, up);

    const float3 pFilmLocal = cameraFilmPointLocal(camera, u, v);
    const float focusScale = camera->focusDistance / camera->nearPlane;
    const float3 pFocusLocal = make_float3(
        pFilmLocal.x * focusScale,
        pFilmLocal.y * focusScale,
        -camera->focusDistance);

    const float3 pFocusWorld = make_float3(
        camera->position.x + rotateByQuat(camera->orientation, pFocusLocal).x,
        camera->position.y + rotateByQuat(camera->orientation, pFocusLocal).y,
        camera->position.z + rotateByQuat(camera->orientation, pFocusLocal).z);

    const float2 disk = sampleConcentricDisk(lensU1, lensU2);
    const float lensOffsetX = disk.x * camera->apertureRadius;
    const float lensOffsetY = disk.y * camera->apertureRadius;

    ro = make_float3(
        camera->position.x + right.x * lensOffsetX + up.x * lensOffsetY,
        camera->position.y + right.y * lensOffsetX + up.y * lensOffsetY,
        camera->position.z + right.z * lensOffsetX + up.z * lensOffsetY);

    rd = normalize3(make_float3(
        pFocusWorld.x - ro.x,
        pFocusWorld.y - ro.y,
        pFocusWorld.z - ro.z));
}
