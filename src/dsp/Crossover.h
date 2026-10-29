// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include <array>
#include <vector>
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
    static std::array<float, 4> enforceOrdering(std::array<float, 4> hz, int numBands,
                                                double baseRate) noexcept;

    // Splits `in` into the first numBands blocks of `bands` (same size as `in`).
    void split(const juce::dsp::AudioBlock<const float>& in, BandBlocks& bands) noexcept;

    // Allpass chain with the crossover phase response (used to phase-match the dry path).
    void processAllpassChain(const juce::dsp::AudioBlock<float>& io) noexcept;

private:
    struct SvfState
    {
        double ic1 = 0.0, ic2 = 0.0;
    };

    struct StageCoefs
    {
        double a1 = 0.0, a2 = 0.0, a3 = 0.0;
    };

    // Log-domain frequency smoother, advanced every kCoefInterval samples. One instance drives split(),
    // another one the dry allpass chain, so both follow the same trajectory when fed the same block sizes.
    struct Smoother
    {
        std::array<double, 4> logHz{};
        std::array<StageCoefs, 4> coefs{};
        int counter = 0;
    };

    struct ChannelState
    {
        // [stage]. The first cascade element of the low- and high-pass sides is the same filter on the same
        // input (lp1 provides both), so it is stored and computed once.
        std::array<SvfState, 4> lp1{}, lp2{}, hp2{};
        std::array<std::array<SvfState, 4>, 3> comp{};     // [band][stage] allpass compensation
        std::array<SvfState, 4> dry{};                     // dry allpass chain
    };

    static constexpr int kCoefInterval = 32;

    void updateTargets() noexcept;
    void snap(Smoother& s) const noexcept;
    void advance(Smoother& s) const noexcept;
    template <typename Fn> void forEachChunk(Smoother& s, int numSamples, Fn&& fn) noexcept;

    double baseRate_ = 44100.0, procRate_ = 44100.0;
    int numBands_ = kMaxBands;
    std::array<float, 4> requested_{120, 1200, 3500, 8000};
    std::array<float, 4> effective_{120, 1200, 3500, 8000};
    std::array<double, 4> targetLogHz_{};
    double smoothAlpha_ = 0.0;
    Smoother splitSmoother_, drySmoother_;
    std::vector<ChannelState> channels_;
};
} // namespace pentagrit
