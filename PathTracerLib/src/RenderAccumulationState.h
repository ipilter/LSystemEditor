#pragma once

#include "PathTracerSampleBudget.h"

#include <QString>

enum class RenderAccumulationState
{
    Accumulating,
    BudgetReached,
    Stopped
};

inline RenderAccumulationState renderAccumulationState(
    bool workerRunning,
    bool userPaused,
    int sampleCount,
    int previewSteps,
    int maxSamplesPerPixel)
{
    if (userPaused || !workerRunning) {
        return RenderAccumulationState::Stopped;
    }
    if (isSampleBudgetExhausted(sampleCount, previewSteps, maxSamplesPerPixel)) {
        return RenderAccumulationState::BudgetReached;
    }
    return RenderAccumulationState::Accumulating;
}

inline QString renderAccumulationStateLabel(RenderAccumulationState state)
{
    switch (state) {
    case RenderAccumulationState::Accumulating:
        return QStringLiteral("Accumulating");
    case RenderAccumulationState::BudgetReached:
        return QStringLiteral("Budget reached");
    case RenderAccumulationState::Stopped:
        return QStringLiteral("Stopped");
    }
    return QStringLiteral("Unknown");
}
