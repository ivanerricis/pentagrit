// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

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
    // Drive compensation: scales the saturator output by 1 / driveGain (the smoothed gain of the pre-gain),
    // so a signal below the knee keeps the level it has without drive and only the harmonic content changes.
    // The curve Off is never compensated. The switch fades in and out over about 20 ms.
    void setDriveComp(bool on) noexcept;
    void process(const juce::dsp::AudioBlock<float>& block) noexcept;

    // Pure transfer function of the curve (also used by the UI curve preview). Hard clip and wavefold are
    // antialiased (ADAA1) in `process`, so their block output is a smoothed version of this curve.
    static float shape(SatType type, float u) noexcept;

private:
    void updateCoefficients() noexcept;

    double rate_ = 44100.0;
    SatType type_ = SatType::off;
    SatType prevType_ = SatType::off;
    float driveDb_ = 6.0f;

    float targetGain_ = 1.9952623f;
    float smoothGain_ = 1.9952623f;
    bool driveSnap_ = true;
    float driveCoeff_ = 0.0f;

    bool compTarget_ = false;
    float compW_ = 0.0f; // 0 = no compensation, 1 = fully compensated; ramps over ~20 ms
    float compStep_ = 0.0f;
    bool compSnap_ = true;

    // Last drive-scaled input sample per channel: state of the first-order ADAA of hard clip / wavefold.
    std::vector<float> uPrev_;

    int fadeLen_ = 480;
    int fadePos_ = 480; // == fadeLen_: no crossfade in progress

    float dcCoeff_ = 0.999f;
    std::vector<float> dcX1_, dcY1_;
};
} // namespace pentagrit
