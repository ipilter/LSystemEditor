#pragma once

#include "Geometry/GeometryTypes.h"

#include <memory>

enum class PrimitiveType
{
    Sphere,
    Cylinder,
    CappedCone,
};

struct ScenePrimitive
{
    PrimitiveType type = PrimitiveType::Sphere;
    Vec3 center{};
    float radius = 0.0f;
    Vec2 halfExtents{};
    float halfHeight = 0.0f;
    float radiusBottom = 0.0f;
    float radiusTop = 0.0f;

    std::unique_ptr<ScenePrimitive> clone() const;
};
