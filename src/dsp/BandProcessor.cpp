// SPDX-License-Identifier: AGPL-3.0-or-later
// STUB (WP0): no-op. Real implementation owned by WP4.
#include "dsp/BandProcessor.h"

namespace pentagrit
{
void BandProcessor::prepare(double procRate, int maxBlock, int numCh)
{
    transient_.prepare(procRate, maxBlock, numCh);
    saturator_.prepare(procRate, maxBlock, numCh);
}

void BandProcessor::setProcessingRate(double procRate) noexcept
{
    transient_.setProcessingRate(procRate);
    saturator_.setProcessingRate(procRate);
}

void BandProcessor::reset() noexcept
{
    transient_.reset();
    saturator_.reset();
}

void BandProcessor::setSettings(const BandSettings& settings, bool audible, float lowFreqHintHz) noexcept
{
    settings_ = settings;
    audible_ = audible;
    transient_.setLowFrequencyHint(lowFreqHintHz);
}

void BandProcessor::process(const juce::dsp::AudioBlock<float>&) noexcept {}
float BandProcessor::getTransientGainDb() const noexcept
{
    return transient_.getLastGainDb();
}
} // namespace pentagrit
