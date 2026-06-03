#pragma once

#include "Sdf/Shapes/SdfShape.h"

#include <memory>
#include <vector>

constexpr float kDefaultSphereRadius = 0.5f;

class SdfAccelScene;

std::vector<std::unique_ptr<SdfShape>> sdfDefaultSceneShapes();
void sdfAccelPopulateScene(SdfAccelScene& scene, const std::vector<std::unique_ptr<SdfShape>>& shapes);
