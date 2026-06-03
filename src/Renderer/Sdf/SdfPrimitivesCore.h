#pragma once

#include "SdfMathCore.h"

SDF_CORE_FN float sdCylinder(SdfFloat3 p, SdfFloat2 h)
{
    SdfFloat2 d = sdfMakeFloat2(sdfLength2(sdfMakeFloat2(p.x, p.z)), sdfFabs(p.y));
    d = sdfMakeFloat2(d.x - h.x, d.y - h.y);
    const float outside = sdfLength2(sdfMakeFloat2(sdfMax2(d.x, 0.0f), sdfMax2(d.y, 0.0f)));
    return sdfMin2(sdfMax2(d.x, d.y), 0.0f) + outside;
}

SDF_CORE_FN float sdSphere(SdfFloat3 p, float radius)
{
    return sdfLength3(p) - radius;
}

SDF_CORE_FN float sdCappedCone(SdfFloat3 p, float halfHeight, float radiusBottom, float radiusTop)
{
    const SdfFloat2 q = sdfMakeFloat2(sdfLength2(sdfMakeFloat2(p.x, p.z)), p.y);
    const SdfFloat2 k1 = sdfMakeFloat2(radiusTop, halfHeight);
    const SdfFloat2 k2 = sdfMakeFloat2(radiusTop - radiusBottom, 2.0f * halfHeight);
    const float capRadius = q.y < 0.0f ? radiusBottom : radiusTop;
    const SdfFloat2 ca = sdfMakeFloat2(q.x - sdfMin2(q.x, capRadius), sdfFabs(q.y) - halfHeight);

    const float k2Dot = k2.x * k2.x + k2.y * k2.y;
    const float t = k2Dot > 0.0f
        ? sdfClamp(((k1.x - q.x) * k2.x + (k1.y - q.y) * k2.y) / k2Dot, 0.0f, 1.0f)
        : 0.0f;
    const SdfFloat2 cb = sdfMakeFloat2(q.x - k1.x + k2.x * t, q.y - k1.y + k2.y * t);
    const float caLenSq = ca.x * ca.x + ca.y * ca.y;
    const float cbLenSq = cb.x * cb.x + cb.y * cb.y;
    const float s = cb.x < 0.0f && ca.y < 0.0f ? -1.0f : 1.0f;
    return s * sqrtf(sdfMin2(caLenSq, cbLenSq));
}
