// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/Meters.h"
#include "ui/Widgets.h"

namespace pentagrit::ui
{
// Small icon button (undo / redo arrows) drawn with the theme palette. Greyed out when disabled.
class HistoryButton final : public juce::Button
{
public:
    enum class Kind
    {
        undo,
        redo
    };
    HistoryButton(Kind kind, const juce::String& name);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    Kind kind() const noexcept { return kind_; }

private:
    Kind kind_;
};

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

    // Undo / redo buttons (right of the preset bar). The owner performs the action and keeps the enabled
    // state in sync with the history through setHistoryState().
    std::function<void()> onUndo, onRedo;
    void setHistoryState(bool canUndo, bool canRedo);
    HistoryButton& undoButton() noexcept { return undo_; }
    HistoryButton& redoButton() noexcept { return redo_; }

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
    HistoryButton undo_{HistoryButton::Kind::undo, "Undo"}, redo_{HistoryButton::Kind::redo, "Redo"};
    SegmentedSelector bands_, os_;
    juce::ToggleButton autoGain_{"AUTO-GAIN"}, bypass_{"BYPASS"}, gear_;
    std::unique_ptr<juce::ButtonParameterAttachment> autoGainAtt_, bypassAtt_;
    juce::Rectangle<int> latencyArea_, bandsLabelArea_, osLabelArea_;
    juce::String latencyText_;
    int lastLatency_ = -1;
    double lastRate_ = -1.0;
};
} // namespace pentagrit::ui
