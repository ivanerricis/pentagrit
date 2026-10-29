// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_dsp/juce_dsp.h>

namespace pentagrit
{
// Differential-envelope transient shaper. Owned by WP2.
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
    double rate_ = 44100.0;
    float attackDb_ = 0, sustainDb_ = 0, speed01_ = 0.5f, lowHintHz_ = 0, lastGainDb_ = 0;
};
} // namespace pentagrit
