// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include <array>
#include <juce_dsp/juce_dsp.h>

namespace pentagrit
{
// Linkwitz-Riley 4th order crossover (2x Butterworth SVF TPT cascade). Owned by WP1.
// prepare() may allocate; every other method is noexcept and allocation free.
class Crossover
{
public:
    using BandBlocks = std::array<juce::dsp::AudioBlock<float>, kMaxBands>;

    void prepare(double baseRate, double procRate, int maxBlock, int numCh);
    void setProcessingRate(double procRate) noexcept;
    void reset() noexcept;

    void setNumBands(int numBands) noexcept;
    void setFrequencies(const std::array<float, 4>& hz) noexcept;
    std::array<float, 4> getEffectiveFrequencies() const noexcept;

    // Enforces ascending order with a minimum distance of 1/3 octave (parameter values are not rewritten).
    static std::array<float, 4> enforceOrdering(std::array<float, 4> hz, int numBands, double baseRate) noexcept;

    // Splits `in` into the first numBands blocks of `bands` (same size as `in`).
    void split(const juce::dsp::AudioBlock<const float>& in, BandBlocks& bands) noexcept;

    // Allpass chain with the crossover phase response (used to phase-match the dry path).
    void processAllpassChain(const juce::dsp::AudioBlock<float>& io) noexcept;

private:
    double baseRate_ = 44100.0, procRate_ = 44100.0;
    int numBands_ = kMaxBands;
    std::array<float, 4> requested_{120, 1200, 3500, 8000};
    std::array<float, 4> effective_{120, 1200, 3500, 8000};
};
} // namespace pentagrit
