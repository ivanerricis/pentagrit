// SPDX-License-Identifier: AGPL-3.0-or-later
// STUB (WP0): no-op / identity curve. Real implementation owned by WP3.
#include "dsp/Saturator.h"

namespace pentagrit
{
void Saturator::prepare(double procRate, int, int) { rate_ = procRate; }
void Saturator::setProcessingRate(double procRate) noexcept { rate_ = procRate; }
void Saturator::reset() noexcept {}
void Saturator::setType(SatType type) noexcept { type_ = type; }
void Saturator::setDriveDb(float driveDb) noexcept { driveDb_ = driveDb; }
void Saturator::process(const juce::dsp::AudioBlock<float>&) noexcept {}
float Saturator::shape(SatType, float u) noexcept { return u; }
} // namespace pentagrit
