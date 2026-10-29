// SPDX-License-Identifier: AGPL-3.0-or-later
// STUB (WP0): no-op. Real implementation owned by WP3.
#include "dsp/AutoGain.h"

namespace pentagrit
{
void AutoGain::prepare(double procRate, int, int) { rate_ = procRate; }
void AutoGain::setProcessingRate(double procRate) noexcept { rate_ = procRate; }
void AutoGain::reset() noexcept { currentGainDb_ = 0.0f; }
void AutoGain::setEnabled(bool enabled) noexcept { enabled_ = enabled; }
void AutoGain::process(const juce::dsp::AudioBlock<const float>&, const juce::dsp::AudioBlock<float>&) noexcept {}
float AutoGain::getCurrentGainDb() const noexcept { return currentGainDb_; }
} // namespace pentagrit
