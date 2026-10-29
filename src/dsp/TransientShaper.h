// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_dsp/juce_dsp.h>

namespace pentagrit
{
// Differential-envelope transient shaper (stereo-linked, zero latency, no lookahead).
class TransientShaper
{
public:
    void prepare(double procRate, int maxBlock, int numCh);
    void setProcessingRate(double procRate) noexcept;
    void reset() noexcept;

    void setParameters(float attackDb, float sustainDb, float speed01) noexcept;
    void setLowFrequencyHint(float hz) noexcept;
    void process(const juce::dsp::AudioBlock<float>& block) noexcept;
    float getLastGainDb() const noexcept;

private:
    void updateCoeffs() noexcept;

    double rate_ = 44100.0;
    float attackDb_ = 0, sustainDb_ = 0, speed01_ = 0.5f, lowHintHz_ = 0, lastGainDb_ = 0;

    // coefficients
    float aFast_ = 0, rE1_ = 0, aE2_ = 0, rE3_ = 0, rE4_ = 0, aGain_ = 0;
    // state
    float e1_ = 0, e2_ = 0, e3_ = 0, e4_ = 0, gSmooth_ = 0;

    // Control-rate gain: the dB mapping, smoothing and dB->linear conversion run every ctrlInterval_ samples;
    // the linear gain is ramped between those points.
    int ctrlInterval_ = 8;
    int ctrlLeft_ = 0;    // samples left until the next control tick (0: tick now)
    float aGainCtrl_ = 0; // gain smoother coefficient for one control interval
    float lin_ = 1.0f, linStep_ = 0.0f, linTarget_ = 1.0f;
};
} // namespace pentagrit
