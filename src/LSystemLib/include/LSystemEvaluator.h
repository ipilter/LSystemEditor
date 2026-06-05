#pragma once

#include "LSystemMaterials.h"
#include "LSystemModel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class LSystem;

/** @brief Final generation and parsed materials from a definition string. */
struct LSystemEvaluationResult
{
    std::vector<Symbol> generation;
    std::vector<MaterialDefinition> materials;
};

class LSystemEvaluator
{
public:
    using Generation = std::vector<Symbol>;
    using StepHistory = std::vector<Generation>;

    /** @brief Parses `definition`, evaluates for `iterations` steps, returns final generation and materials. */
    static LSystemEvaluationResult evaluate(const std::string& definition,
        std::size_t iterations,
        std::uint64_t seed = 0xC0FFEEULL);

    // Evaluates the parsed L-system for `iterations` derivation steps and returns
    // all generations, including generation 0 (the original axiom).
    //
    // Matching/evaluation semantics:
    // - Rule matching is simultaneous per generation.
    // - A predecessor/context argument expression that is an identifier binds to
    //   the actual numeric argument value of the currently matched symbol.
    // - Repeated identifiers must match the same numeric value.
    // - Conditions are numeric; zero is false, non-zero is true.
    // - If several rules match the same predecessor, one rule is selected
    //   stochastically using each candidate rule's `probability` as a weight.
    //   If no positive finite weights exist, the first matching rule is used.
    static StepHistory evaluate_steps(const LSystem& system,
        std::size_t iterations,
        std::uint64_t seed = 0xC0FFEEULL);
};
