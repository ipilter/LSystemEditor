#pragma once

#include "MeshAccelTypes.h"
#include "Geometry/MathCore.h"

#if defined(__CUDACC__)
#define MESH_ACCEL_CORE_FN __host__ __device__ inline
#else
#define MESH_ACCEL_CORE_FN inline
#endif

MESH_ACCEL_CORE_FN Vec3 meshAccelTriangleGeometricNormal(const TriangleGpu& tri)
{
    const Vec3 e1 = vecSub3(tri.v1, tri.v0);
    const Vec3 e2 = vecSub3(tri.v2, tri.v0);
    const Vec3 cross = vecCross3(e1, e2);
    const float len = vecLength3(cross);
    if (len <= 1.0e-12f) {
        return vecMake3(0.0f, 0.0f, 1.0f);
    }
    return vecScale3(cross, 1.0f / len);
}

MESH_ACCEL_CORE_FN float meshAccelTriangleDoubleArea(const TriangleGpu& tri)
{
    const Vec3 e1 = vecSub3(tri.v1, tri.v0);
    const Vec3 e2 = vecSub3(tri.v2, tri.v0);
    return vecLength3(vecCross3(e1, e2));
}

MESH_ACCEL_CORE_FN bool meshAccelRayAabb(
    Vec3 ro,
    Vec3 rd,
    Vec3 invRd,
    Vec3 boundsMin,
    Vec3 boundsMax,
    float tMin,
    float tMax,
    float& outTEnter,
    float& outTExit)
{
    float tEnter = tMin;
    float tExit = tMax;

    for (int axis = 0; axis < 3; ++axis) {
        const float origin = axis == 0 ? ro.x : (axis == 1 ? ro.y : ro.z);
        const float invD = axis == 0 ? invRd.x : (axis == 1 ? invRd.y : invRd.z);
        const float bMin = axis == 0 ? boundsMin.x : (axis == 1 ? boundsMin.y : boundsMin.z);
        const float bMax = axis == 0 ? boundsMax.x : (axis == 1 ? boundsMax.y : boundsMax.z);

        if (fabsf(invD) <= 1.0e-8f) {
            if (origin < bMin || origin > bMax) {
                return false;
            }
            continue;
        }

        float t0 = (bMin - origin) * invD;
        float t1 = (bMax - origin) * invD;
        if (invD < 0.0f) {
            const float tmp = t0;
            t0 = t1;
            t1 = tmp;
        }

        tEnter = vecMax2(tEnter, t0);
        tExit = vecMin2(tExit, t1);
        if (tEnter > tExit) {
            return false;
        }
    }

    outTEnter = tEnter;
    outTExit = tExit;
    return tExit >= tMin;
}

MESH_ACCEL_CORE_FN bool meshAccelRayTriangle(
    Vec3 ro,
    Vec3 rd,
    const TriangleGpu& tri,
    float tMin,
    float tMax,
    float& outT,
    Vec3& outNormal,
    Vec2& outUv)
{
    const Vec3 e1 = vecSub3(tri.v1, tri.v0);
    const Vec3 e2 = vecSub3(tri.v2, tri.v0);
    const Vec3 pvec = vecMake3(
        rd.y * e2.z - rd.z * e2.y,
        rd.z * e2.x - rd.x * e2.z,
        rd.x * e2.y - rd.y * e2.x);

    const float det = vecDot3(e1, pvec);
    if (fabsf(det) < 1.0e-8f) {
        return false;
    }

    const float invDet = 1.0f / det;
    const Vec3 tvec = vecSub3(ro, tri.v0);
    const float baryU = vecDot3(tvec, pvec) * invDet;
    if (baryU < 0.0f || baryU > 1.0f) {
        return false;
    }

    const Vec3 qvec = vecMake3(
        tvec.y * e1.z - tvec.z * e1.y,
        tvec.z * e1.x - tvec.x * e1.z,
        tvec.x * e1.y - tvec.y * e1.x);
    const float baryV = vecDot3(rd, qvec) * invDet;
    if (baryV < 0.0f || baryU + baryV > 1.0f) {
        return false;
    }

    const float t = vecDot3(e2, qvec) * invDet;
    if (t < tMin || t > tMax) {
        return false;
    }

    const float baryW = 1.0f - baryU - baryV;
    outT = t;

    const Vec3 geomNormal = meshAccelTriangleGeometricNormal(tri);
    const float doubleArea = meshAccelTriangleDoubleArea(tri);
    Vec3 n = vecAdd3(
        vecAdd3(vecScale3(tri.n0, baryW), vecScale3(tri.n1, baryU)),
        vecScale3(tri.n2, baryV));
    const float shadingNormalLen = vecLength3(n);
    if (shadingNormalLen <= 1.0e-4f || doubleArea <= 1.0e-6f) {
        n = geomNormal;
    } else {
        n = vecScale3(n, 1.0f / shadingNormalLen);
        if (vecDot3(n, geomNormal) < 0.0f) {
            n = geomNormal;
        }
    }
    if (vecDot3(n, rd) > 0.0f) {
        n = vecScale3(n, -1.0f);
    }
    outNormal = n;
    outUv = Vec2{
        tri.uv0.x * baryW + tri.uv1.x * baryU + tri.uv2.x * baryV,
        tri.uv0.y * baryW + tri.uv1.y * baryU + tri.uv2.y * baryV};
    return true;
}

MESH_ACCEL_CORE_FN MeshHit meshAccelTraceRay(
    Vec3 ro,
    Vec3 rd,
    const MeshAccelSceneGpu* scene,
    float tMin,
    float tMax)
{
    MeshHit result{};

    if (scene == nullptr || scene->bvhNodes == nullptr || scene->triangles == nullptr ||
        scene->bvhNodeCount == 0 || scene->triangleCount == 0) {
        return result;
    }

    rd = vecNormalize3(rd);
    const Vec3 invRd = vecMake3(
        fabsf(rd.x) > 1.0e-8f ? 1.0f / rd.x : 0.0f,
        fabsf(rd.y) > 1.0e-8f ? 1.0f / rd.y : 0.0f,
        fabsf(rd.z) > 1.0e-8f ? 1.0f / rd.z : 0.0f);

    int stack[64];
    int stackSize = 0;
    stack[stackSize++] = static_cast<int>(scene->bvhRootIndex);

    float closestT = tMax;
    Vec3 closestNormal{};
    Vec2 closestUv{};
    uint32_t closestTriangleIndex = 0;

    while (stackSize > 0) {
        const int nodeIndex = stack[--stackSize];
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(scene->bvhNodeCount)) {
            continue;
        }

        const MeshBvhNode& node = scene->bvhNodes[nodeIndex];
        float tEnter = 0.0f;
        float tExit = 0.0f;
        if (!meshAccelRayAabb(ro, rd, invRd, node.boundsMin, node.boundsMax, tMin, closestT, tEnter, tExit)) {
            continue;
        }

        if (node.triCount > 0) {
            const uint32_t end = node.triStart + node.triCount;
            for (uint32_t triIndex = node.triStart; triIndex < end; ++triIndex) {
                if (triIndex >= scene->triangleCount) {
                    break;
                }

                float tHit = 0.0f;
                Vec3 normal{};
                Vec2 uv{};
                if (meshAccelRayTriangle(ro, rd, scene->triangles[triIndex], tMin, closestT, tHit, normal, uv)) {
                    closestT = tHit;
                    closestNormal = normal;
                    closestUv = uv;
                    closestTriangleIndex = triIndex;
                }
            }
            continue;
        }

        if (stackSize + 2 > 64) {
            continue;
        }

        const int left = static_cast<int>(node.leftIndex);
        const int right = static_cast<int>(node.rightIndex);

        float leftEnter = 0.0f;
        float leftExit = 0.0f;
        float rightEnter = 0.0f;
        float rightExit = 0.0f;
        bool leftHit = false;
        bool rightHit = false;

        if (left >= 0 && left < static_cast<int>(scene->bvhNodeCount)) {
            const MeshBvhNode& leftNode = scene->bvhNodes[left];
            leftHit = meshAccelRayAabb(
                ro, rd, invRd, leftNode.boundsMin, leftNode.boundsMax, tMin, closestT, leftEnter, leftExit);
        }
        if (right >= 0 && right < static_cast<int>(scene->bvhNodeCount)) {
            const MeshBvhNode& rightNode = scene->bvhNodes[right];
            rightHit = meshAccelRayAabb(
                ro, rd, invRd, rightNode.boundsMin, rightNode.boundsMax, tMin, closestT, rightEnter, rightExit);
        }

        if (leftHit && rightHit) {
            if (leftEnter < rightEnter) {
                stack[stackSize++] = right;
                stack[stackSize++] = left;
            } else {
                stack[stackSize++] = left;
                stack[stackSize++] = right;
            }
        } else if (leftHit) {
            stack[stackSize++] = left;
        } else if (rightHit) {
            stack[stackSize++] = right;
        }
    }

    if (closestT < tMax) {
        result.hit = true;
        result.t = closestT;
        result.normal = vecNormalize3(closestNormal);
        result.uv = closestUv;
        result.triangleIndex = closestTriangleIndex;
    }

    return result;
}
