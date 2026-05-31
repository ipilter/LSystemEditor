#pragma once

#include "SdfTypes.h"

#include <cmath>

#if defined(__CUDACC__)
#define SDF_CORE_FN __host__ __device__ inline
#else
#define SDF_CORE_FN inline
#endif

SDF_CORE_FN SdfFloat3 sdfMakeFloat3(float x, float y, float z)
{
    return SdfFloat3{x, y, z};
}

SDF_CORE_FN SdfFloat2 sdfMakeFloat2(float x, float y)
{
    return SdfFloat2{x, y};
}

SDF_CORE_FN float sdfDot3(SdfFloat3 a, SdfFloat3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

SDF_CORE_FN float sdfLength3(SdfFloat3 v)
{
    return sqrtf(sdfDot3(v, v));
}

SDF_CORE_FN float sdfLength2(SdfFloat2 v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

SDF_CORE_FN SdfFloat3 sdfAdd3(SdfFloat3 a, SdfFloat3 b)
{
    return sdfMakeFloat3(a.x + b.x, a.y + b.y, a.z + b.z);
}

SDF_CORE_FN SdfFloat3 sdfSub3(SdfFloat3 a, SdfFloat3 b)
{
    return sdfMakeFloat3(a.x - b.x, a.y - b.y, a.z - b.z);
}

SDF_CORE_FN SdfFloat3 sdfScale3(SdfFloat3 v, float s)
{
    return sdfMakeFloat3(v.x * s, v.y * s, v.z * s);
}

SDF_CORE_FN SdfFloat3 sdfNormalize3(SdfFloat3 v)
{
    const float len = sdfLength3(v);
    if (len <= 0.0f) {
        return sdfMakeFloat3(0.0f, 0.0f, -1.0f);
    }
    const float invLen = 1.0f / len;
    return sdfScale3(v, invLen);
}

SDF_CORE_FN SdfFloat3 sdfEvalRay(SdfFloat3 ro, SdfFloat3 rd, float t)
{
    return sdfAdd3(ro, sdfScale3(rd, t));
}

SDF_CORE_FN float sdfMax2(float a, float b)
{
    return a > b ? a : b;
}

SDF_CORE_FN float sdfMin2(float a, float b)
{
    return a < b ? a : b;
}

SDF_CORE_FN float sdfMax3(float a, float b, float c)
{
    return sdfMax2(sdfMax2(a, b), c);
}

SDF_CORE_FN float sdfMin3(float a, float b, float c)
{
    return sdfMin2(sdfMin2(a, b), c);
}

SDF_CORE_FN float sdfAbs(float v)
{
    return v < 0.0f ? -v : v;
}

SDF_CORE_FN float sdfFabs(float v)
{
    return sdfAbs(v);
}

SDF_CORE_FN float sdfClamp(float value, float low, float high)
{
    return sdfMax2(low, sdfMin2(value, high));
}

SDF_CORE_FN float sdfOpUnion(float a, float b)
{
    return sdfMin2(a, b);
}
