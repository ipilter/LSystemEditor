#include "SdfShape.h"

#include "SdfMathCore.h"

float SdfShape::evalWorld(SdfFloat3 worldP) const
{
    return evalLocal(sdfSub3(worldP, center()));
}
