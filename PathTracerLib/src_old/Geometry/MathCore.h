#pragma once

#include "Geometry/GeometryTypes.h"

#include <cmath>

#if defined(__CUDACC__)
#define MATH_CORE_FN __host__ __device__ inline
#else
#define MATH_CORE_FN inline
#endif

MATH_CORE_FN Vec3 vecMake3(float x, float y, float z)
{
    return Vec3{x, y, z};
}

MATH_CORE_FN Vec2 vecMake2(float x, float y)
{
    return Vec2{x, y};
}

MATH_CORE_FN float vecDot3(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

MATH_CORE_FN float vecLength3(Vec3 v)
{
    return sqrtf(vecDot3(v, v));
}

MATH_CORE_FN float vecLength2(Vec2 v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

MATH_CORE_FN Vec3 vecAdd3(Vec3 a, Vec3 b)
{
    return vecMake3(a.x + b.x, a.y + b.y, a.z + b.z);
}

MATH_CORE_FN Vec3 vecSub3(Vec3 a, Vec3 b)
{
    return vecMake3(a.x - b.x, a.y - b.y, a.z - b.z);
}

MATH_CORE_FN Vec3 vecScale3(Vec3 v, float s)
{
    return vecMake3(v.x * s, v.y * s, v.z * s);
}

MATH_CORE_FN Vec3 vecNormalize3(Vec3 v)
{
    const float len = vecLength3(v);
    if (len <= 0.0f) {
        return vecMake3(0.0f, 0.0f, -1.0f);
    }
    const float invLen = 1.0f / len;
    return vecScale3(v, invLen);
}

MATH_CORE_FN Vec3 vecEvalRay(Vec3 ro, Vec3 rd, float t)
{
    return vecAdd3(ro, vecScale3(rd, t));
}

MATH_CORE_FN float vecMax2(float a, float b)
{
    return a > b ? a : b;
}

MATH_CORE_FN float vecMin2(float a, float b)
{
    return a < b ? a : b;
}

MATH_CORE_FN float vecMax3(float a, float b, float c)
{
    return vecMax2(vecMax2(a, b), c);
}

MATH_CORE_FN float vecMin3(float a, float b, float c)
{
    return vecMin2(vecMin2(a, b), c);
}

MATH_CORE_FN float vecAbs(float v)
{
    return v < 0.0f ? -v : v;
}

MATH_CORE_FN float vecClamp(float value, float low, float high)
{
    return vecMax2(low, vecMin2(value, high));
}

MATH_CORE_FN Vec3 vecCross3(Vec3 a, Vec3 b)
{
    return vecMake3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

MATH_CORE_FN Vec3 vecLerp3(Vec3 a, Vec3 b, float t)
{
    return vecAdd3(vecScale3(a, 1.0f - t), vecScale3(b, t));
}

MATH_CORE_FN float vecLerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

#if !defined(__CUDACC__)
inline Vec3 vecRotateYawPitchRoll(Vec3 v, float yawDeg, float pitchDeg, float rollDeg)
{
    const float degToRad = 3.14159265f / 180.0f;
    const float yaw = yawDeg * degToRad;
    const float pitch = pitchDeg * degToRad;
    const float roll = rollDeg * degToRad;

    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    Vec3 yRot = vecMake3(cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z);

    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    Vec3 xRot = vecMake3(yRot.x, cp * yRot.y - sp * yRot.z, sp * yRot.y + cp * yRot.z);

    const float cr = std::cos(roll);
    const float sr = std::sin(roll);
    return vecMake3(cr * xRot.x + sr * xRot.y, -sr * xRot.x + cr * xRot.y, xRot.z);
}
#endif
