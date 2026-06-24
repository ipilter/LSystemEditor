#include "MeshSceneContent.h"

bool meshSceneBuild(const Mesh& mesh, MeshAccelScene& scene, QString* outError)
{
    (void)outError;
    return scene.build(mesh);
}
