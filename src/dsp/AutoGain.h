// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_dsp/juce_dsp.h>

namespace pentagrit
{
// Matches the wet RMS to a dry reference (400 ms window, +-24 dB clamp). Owned by WP3.
class AutoGain
{
public:
    void prepare(double procRate, int maxBlock, int numCh);
    void setProcessingRate(double procRate) noexcept;
    void reset() noexcept;

    void setEnabled(bool enabled) noexcept;
    // Measures `ref` (dry) against `wet` and applies the compensation gain to `wet` in place.
    void process(const juce::dsp::AudioBlock<const float>& ref, const juce::dsp::AudioBlock<float>& wet) noexcept;
    float getCurrentGainDb() const noexcept;

private:
    double rate_ = 44100.0;
    bool enabled_ = false;
    float currentGainDb_ = 0.0f;
};
} // namespace pentagrit
