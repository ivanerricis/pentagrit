// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "params/ParamIDs.h"
#include <array>
#include <atomic>

namespace pentagrit
{
// Audio thread writes with relaxed ordering, UI thread reads.
struct MeterState
{
    MeterState() noexcept
    {
        for (auto& v : bandPeakDb)
            v.store(-100.0f, std::memory_order_relaxed);
        for (auto& v : bandTransientGainDb)
            v.store(0.0f, std::memory_order_relaxed);
        constexpr std::array<float, 4> defaults{120.0f, 1200.0f, 3500.0f, 8000.0f};
        for (size_t i = 0; i < defaults.size(); ++i)
            effectiveXoverHz[i].store(defaults[i], std::memory_order_relaxed);
    }

    std::array<std::atomic<float>, kMaxBands> bandPeakDb, bandTransientGainDb;
    std::atomic<float> inPeakDb{-100}, outPeakDb{-100}, autoGainDb{0};
    std::array<std::atomic<float>, 4> effectiveXoverHz;
    std::atomic<int> latencySamples{0};
    std::atomic<float> cpuLoad{0.0f}; // fraction 0..1 of block time spent in processBlock
};
} // namespace pentagrit
