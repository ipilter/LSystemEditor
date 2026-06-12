#pragma once

#include "ProceduralTypes.h"
#include "Spline.h"

#include <manifold/manifold.h>

manifold::Manifold loftSegment(const SplinePath& path, const ProceduralBuildParams& params);
manifold::Manifold loftOrSphereFromSegment(const TurtleSegment& segment, const ProceduralBuildParams& params);
