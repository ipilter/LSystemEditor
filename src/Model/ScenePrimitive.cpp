#include "ScenePrimitive.h"

std::unique_ptr<ScenePrimitive> ScenePrimitive::clone() const
{
    auto copy = std::make_unique<ScenePrimitive>();
    copy->type = type;
    copy->center = center;
    copy->radius = radius;
    copy->halfExtents = halfExtents;
    copy->halfHeight = halfHeight;
    copy->radiusBottom = radiusBottom;
    copy->radiusTop = radiusTop;
    return copy;
}
