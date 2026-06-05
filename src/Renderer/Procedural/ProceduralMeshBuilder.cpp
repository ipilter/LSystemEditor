#include "ProceduralMeshBuilder.h"

#include "LSystemEvaluator.h"
#include "ManifoldMeshConvert.h"
#include "ProceduralAssembly.h"
#include "Turtle.h"

#include <manifold/manifold.h>
#include <string>

namespace {

bool isValidManifold(const manifold::Manifold& mesh)
{
    return mesh.Status() == manifold::Manifold::Error::NoError && mesh.NumTri() > 0;
}

manifold::Manifold applyRootTransform(const manifold::Manifold& mesh, const RootTransform& root)
{
    const double yaw = static_cast<double>(root.rotationDeg.x);
    const double pitch = static_cast<double>(root.rotationDeg.y);
    const double roll = static_cast<double>(root.rotationDeg.z);
    return mesh.Rotate(0.0, yaw, 0.0)
        .Rotate(pitch, 0.0, 0.0)
        .Rotate(0.0, 0.0, roll)
        .Translate(manifold::vec3(root.translation.x, root.translation.y, root.translation.z));
}

} // namespace

bool ProceduralMeshBuilder::buildHostMesh(
    std::string_view definition,
    const std::size_t iterations,
    const RootTransform& root,
    HostMesh& outMesh,
    const ProceduralBuildParams& params)
{
    outMesh.triangles.clear();

    const LSystemEvaluationResult eval =
        LSystemEvaluator::evaluate(std::string(definition), iterations);
    const TurtleOutput turtleOutput = turtleExecute(eval.generation, params.turtle);
    if (turtleOutput.segments.empty()) {
        return false;
    }

    manifold::Manifold assembled = assembleLofts(turtleOutput, params);
    if (!isValidManifold(assembled)) {
        return false;
    }

    assembled = applyRootTransform(assembled, root);
    if (!isValidManifold(assembled)) {
        return false;
    }

    assembled = assembled.CalculateNormals(0);
    if (!isValidManifold(assembled)) {
        return false;
    }

    outMesh = meshFromManifold(assembled);
    return !outMesh.triangles.empty();
}
