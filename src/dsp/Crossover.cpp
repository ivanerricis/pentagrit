// SPDX-License-Identifier: AGPL-3.0-or-later
// STUB (WP0): routes everything to band 0. Real implementation owned by WP1.
#include "dsp/Crossover.h"

#include <algorithm>
#include <cmath>

namespace pentagrit
{
void Crossover::prepare(double baseRate, double procRate, int, int)
{
    baseRate_ = baseRate;
    procRate_ = procRate;
}

void Crossover::setProcessingRate(double procRate) noexcept { procRate_ = procRate; }

void Crossover::reset() noexcept {}

void Crossover::setNumBands(int numBands) noexcept
{
    numBands_ = std::clamp(numBands, 2, kMaxBands);
    effective_ = enforceOrdering(requested_, numBands_, baseRate_);
}

void Crossover::setFrequencies(const std::array<float, 4>& hz) noexcept
{
    requested_ = hz;
    effective_ = enforceOrdering(requested_, numBands_, baseRate_);
}

std::array<float, 4> Crossover::getEffectiveFrequencies() const noexcept { return effective_; }

std::array<float, 4> Crossover::enforceOrdering(std::array<float, 4> hz, int numBands, double baseRate) noexcept
{
    const float ratio = std::pow(2.0f, 1.0f / 3.0f);
    const float maxHz = static_cast<float>(0.45 * baseRate);
    const int active = std::clamp(numBands, 2, kMaxBands) - 1;
    float prev = 0.0f;
    for (int i = 0; i < active; ++i)
    {
        auto& f = hz[static_cast<size_t>(i)];
        f = std::max(f, i == 0 ? 20.0f : prev * ratio);
        f = std::min(f, maxHz);
        prev = f;
    }
    return hz;
}

void Crossover::split(const juce::dsp::AudioBlock<const float>& in, BandBlocks& bands) noexcept
{
    bands[0].copyFrom(in);
    for (int b = 1; b < numBands_; ++b)
        bands[static_cast<size_t>(b)].clear();
}

void Crossover::processAllpassChain(const juce::dsp::AudioBlock<float>&) noexcept {}
} // namespace pentagrit
