// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include "dsp/Saturator.h"
#include "dsp/TransientShaper.h"
#include <vector>
#include <juce_dsp/juce_dsp.h>

namespace pentagrit
{
// One band: TransientShaper -> Saturator -> dry/wet mix -> gain x audibility (5 ms ramps). Owned by WP4.
// Band bypass skips shaper, saturator and mix (gain and mute/solo still apply); a 5 ms crossfade hides the
// switch and the stateful modules are reset once the band is fully bypassed.
// Lookahead: with a delay D > 0 the band audio is delayed by D samples (always, also when bypassed, muted or
// neutral) while the transient detector sees the undelayed signal, so the gain lands on the transient.
class BandProcessor
{
public:
    // maxDelaySamples: largest lookahead delay (processing-rate samples) that setLookahead() will be given.
    void prepare(double procRate, int maxBlock, int numCh, int maxDelaySamples = 0);
    void setProcessingRate(double procRate) noexcept;
    // Lookahead delay in processing-rate samples (0 = off). Clears the delay history.
    void setLookahead(int delaySamples) noexcept;
    void reset() noexcept;

    void setSettings(const BandSettings& settings, bool audible, float lowFreqHintHz) noexcept;
    void process(const juce::dsp::AudioBlock<float>& block) noexcept;
    float getTransientGainDb() const noexcept;
    // Transient gain range (dB) since the last call; 0/0 while the band is fully bypassed (cold).
    GainRangeDb consumeTransientGainRange() noexcept;

private:
    TransientShaper transient_;
    Saturator saturator_;
    BandSettings settings_;
    bool audible_ = true;

    double rate_ = 44100.0;
    juce::AudioBuffer<float> dry_;
    juce::AudioBuffer<float> detector_; // undelayed copy of the input (lookahead only)
    juce::AudioBuffer<float> delayLine_;
    int delayMask_ = 0, delayPos_ = 0, delay_ = 0;
    std::vector<float> gainRamp_, mixRamp_;
    juce::SmoothedValue<float> gain_, wetMix_;
    bool needSnap_ = true;
    bool cold_ = false; // modules reset while fully bypassed
};
} // namespace pentagrit
