// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/Meters.h"
#include "ui/Widgets.h"

namespace pentagrit::ui
{
// Top bar: logo, [preset slot for WP8's PresetBar], bands selector, oversampling selector, latency readout,
// AUTO-GAIN, BYPASS and the settings gear.
class TopBar final : public juce::Component
{
public:
    // `sampleRate` is queried for the latency readout in ms (may return 0 when unknown).
    TopBar(juce::AudioProcessorValueTreeState& apvts, MeterState& meters, std::function<double()> sampleRate);

    // WP8: add the PresetBar as a child of this component. It is stretched over the slot automatically.
    juce::Component& presetSlot() noexcept { return presetSlot_; }
    // WP9: set this to open the settings panel. The gear is also reachable as `settingsButton()`.
    std::function<void()> onSettingsClicked;
    juce::ToggleButton& settingsButton() noexcept { return gear_; }

    void applyTheme();
    void tick();

    juce::String latencyText() const { return latencyText_; }

    void resized() override;
    void paint(juce::Graphics&) override;

    static void drawLogoMark(juce::Graphics&, juce::Rectangle<float> area, const Palette&);

private:
    MeterState& meters_;
    std::function<double()> sampleRate_;
    SlotComponent presetSlot_;
    SegmentedSelector bands_, os_;
    juce::ToggleButton autoGain_{"AUTO-GAIN"}, bypass_{"BYPASS"}, gear_;
    std::unique_ptr<juce::ButtonParameterAttachment> autoGainAtt_, bypassAtt_;
    juce::Rectangle<int> latencyArea_, bandsLabelArea_, osLabelArea_;
    juce::String latencyText_;
    int lastLatency_ = -1;
    double lastRate_ = -1.0;
};
} // namespace pentagrit::ui
