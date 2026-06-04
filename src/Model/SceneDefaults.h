#pragma once

#include "ScenePrimitive.h"

#include <memory>
#include <vector>

constexpr float kDefaultSphereRadius = 0.5f;

std::vector<std::unique_ptr<ScenePrimitive>> defaultScenePrimitives();
