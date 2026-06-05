#pragma once

#include "LSystemModel.h"
#include "ProceduralTypes.h"

#include <vector>

TurtleOutput turtleExecute(const std::vector<Symbol>& symbols, const TurtleParams& params = TurtleParams{});
