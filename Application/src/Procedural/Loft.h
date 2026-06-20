#pragma once

#include "MeshAccel/Mesh.h"
#include "ProceduralTypes.h"
#include "Spline.h"

#include <manifold/manifold.h>

manifold::Manifold loftSegment(const SplinePath& path, const ProceduralBuildParams& params);
manifold::Manifold loftOrSphereFromSegment(const TurtleSegment& segment, const ProceduralBuildParams& params);
manifold::Manifold refineManifoldForRender(
    const manifold::Manifold& mesh,
    const ProceduralBuildParams& params,
    float characteristicRadius);
float characteristicRadiusFromSegment(const TurtleSegment& segment, const ProceduralBuildParams& params);
Mesh renderMeshFromManifold(const manifold::Manifold& mesh, const ProceduralBuildParams& params, float characteristicRadius);
Mesh renderMeshFromSegment(const TurtleSegment& segment, const ProceduralBuildParams& params);
