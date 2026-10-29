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
class BandProcessor
{
public:
    void prepare(double procRate, int maxBlock, int numCh);
    void setProcessingRate(double procRate) noexcept;
    void reset() noexcept;

    void setSettings(const BandSettings& settings, bool audible, float lowFreqHintHz) noexcept;
    void process(const juce::dsp::AudioBlock<float>& block) noexcept;
    float getTransientGainDb() const noexcept;

private:
    TransientShaper transient_;
    Saturator saturator_;
    BandSettings settings_;
    bool audible_ = true;

    double rate_ = 44100.0;
    juce::AudioBuffer<float> dry_;
    std::vector<float> gainRamp_, mixRamp_;
    juce::SmoothedValue<float> gain_, wetMix_;
    bool needSnap_ = true;
    bool cold_ = false; // modules reset while fully bypassed
};
} // namespace pentagrit
