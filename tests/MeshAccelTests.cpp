#include "MeshAccel/MeshAccelScene.h"
#include "MeshAccel/MeshAccelTypes.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>

int gFailures = 0;

namespace {

void expectTrue(bool condition, const char* label)
{
    if (!condition) {
        ++gFailures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

void testGpuStructLayout()
{
    expectTrue(sizeof(TriangleGpu) == 64, "TriangleGpuSize");
    expectTrue(sizeof(MeshBvhNode) == 48, "MeshBvhNodeSize");
    expectTrue(sizeof(MeshAccelSceneGpu) == 48, "MeshAccelSceneGpuSize");
    expectTrue(offsetof(MeshBvhNode, leftIndex) == 32, "MeshBvhNodeLeftIndexOffset");
}

} // namespace

void runProceduralMeshTests();

int main()
{
    testGpuStructLayout();
    runProceduralMeshTests();

    if (gFailures != 0) {
        std::cerr << gFailures << " test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "All mesh accel tests passed\n";
    return EXIT_SUCCESS;
}
