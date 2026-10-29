// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include "dsp/Saturator.h"
#include "dsp/TransientShaper.h"
#include <juce_dsp/juce_dsp.h>

namespace pentagrit
{
// One band: TransientShaper -> Saturator -> dry/wet mix -> gain x mute/solo. Owned by WP4.
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
};
} // namespace pentagrit
