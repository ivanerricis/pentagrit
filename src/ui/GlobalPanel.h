// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "dsp/Meters.h"
#include "ui/Widgets.h"

namespace pentagrit::ui
{
// Master column: In / Out knobs with meters, global Mix, drive compensation switch, MONITOR row (delta and
// level-matched bypass), auto-gain readout.
class GlobalPanel final : public juce::Component
{
public:
    explicit GlobalPanel(PentagritAudioProcessor& processor);

    void applyTheme();
    void applyOptions(const UiOptions& o);
    void tick(float dtSec);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void syncMonitor();

    PentagritAudioProcessor& processor_;
    MeterState& meters_;
    std::atomic<float>* autoGainOn_;
    LabelledKnob in_, out_, mix_;
    LevelMeter inMeter_, outMeter_;
    juce::ToggleButton driveComp_{"DRIVE COMP"};
    std::unique_ptr<juce::ButtonParameterAttachment> driveCompAtt_;
    juce::ToggleButton delta_{"DELTA"}, matchBypass_{"MATCH BYPASS"};
    juce::Rectangle<int> monitorHead_, deltaCaption_;
    juce::String agText_, matchText_;
    bool deltaShown_ = false;
};
} // namespace pentagrit::ui
