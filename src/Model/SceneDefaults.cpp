#include "SceneDefaults.h"

std::vector<std::unique_ptr<ScenePrimitive>> defaultScenePrimitives()
{
    std::vector<std::unique_ptr<ScenePrimitive>> primitives;
    auto sphere = std::make_unique<ScenePrimitive>();
    sphere->type = PrimitiveType::Sphere;
    sphere->center = Vec3{0.0f, 0.0f, 0.0f};
    sphere->radius = kDefaultSphereRadius;
    primitives.push_back(std::move(sphere));
    return primitives;
}
