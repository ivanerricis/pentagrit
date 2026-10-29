// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/Meters.h"
#include "ui/Widgets.h"

namespace pentagrit::ui
{
// Master column: In / Out knobs with meters, global Mix, auto-gain readout.
class GlobalPanel final : public juce::Component
{
public:
    GlobalPanel(juce::AudioProcessorValueTreeState& apvts, MeterState& meters);

    void applyTheme();
    void applyOptions(const UiOptions& o);
    void tick(float dtSec);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    MeterState& meters_;
    std::atomic<float>* autoGainOn_;
    LabelledKnob in_, out_, mix_;
    LevelMeter inMeter_, outMeter_;
    juce::String agText_;
};
} // namespace pentagrit::ui
