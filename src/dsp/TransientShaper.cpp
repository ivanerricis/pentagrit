// SPDX-License-Identifier: AGPL-3.0-or-later
// STUB (WP0): no-op. Real implementation owned by WP2.
#include "dsp/TransientShaper.h"

namespace pentagrit
{
void TransientShaper::prepare(double procRate, int, int) { rate_ = procRate; }
void TransientShaper::setProcessingRate(double procRate) noexcept { rate_ = procRate; }
void TransientShaper::reset() noexcept { lastGainDb_ = 0.0f; }

void TransientShaper::setParameters(float attackDb, float sustainDb, float speed01) noexcept
{
    attackDb_ = attackDb;
    sustainDb_ = sustainDb;
    speed01_ = speed01;
}

void TransientShaper::setLowFrequencyHint(float hz) noexcept { lowHintHz_ = hz; }
void TransientShaper::process(const juce::dsp::AudioBlock<float>&) noexcept {}
float TransientShaper::getLastGainDb() const noexcept { return lastGainDb_; }
} // namespace pentagrit
