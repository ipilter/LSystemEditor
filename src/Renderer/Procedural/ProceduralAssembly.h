#pragma once

#include "ProceduralTypes.h"

#include <manifold/manifold.h>

manifold::Manifold assembleLofts(const TurtleOutput& output, const ProceduralBuildParams& params);
