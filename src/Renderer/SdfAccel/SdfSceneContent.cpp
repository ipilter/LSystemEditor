#include "SdfSceneContent.h"

#include "Sdf/Shapes/SphereSdf.h"
#include "SdfAccelScene.h"

std::vector<std::unique_ptr<SdfShape>> sdfDefaultSceneShapes()
{
    std::vector<std::unique_ptr<SdfShape>> shapes;
    shapes.push_back(std::make_unique<SphereSdf>(
        sdfMakeFloat3(0.0f, 0.0f, 0.0f),
        kDefaultSphereRadius));
    return shapes;
}

void sdfAccelPopulateScene(SdfAccelScene& scene, const std::vector<std::unique_ptr<SdfShape>>& shapes)
{
    scene.clear();
    for (const std::unique_ptr<SdfShape>& shape : shapes) {
        if (shape != nullptr) {
            scene.addShape(*shape);
        }
    }
}
