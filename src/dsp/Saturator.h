// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include <juce_dsp/juce_dsp.h>

namespace pentagrit
{
// Waveshaping saturator with drive. Owned by WP3.
class Saturator
{
public:
    void prepare(double procRate, int maxBlock, int numCh);
    void setProcessingRate(double procRate) noexcept;
    void reset() noexcept;

    void setType(SatType type) noexcept;
    void setDriveDb(float driveDb) noexcept;
    void process(const juce::dsp::AudioBlock<float>& block) noexcept;

    // Pure transfer function of the curve (also used by the UI curve preview).
    static float shape(SatType type, float u) noexcept;

private:
    double rate_ = 44100.0;
    SatType type_ = SatType::off;
    float driveDb_ = 6.0f;
};
} // namespace pentagrit
