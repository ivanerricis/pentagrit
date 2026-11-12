// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/Meters.h"
#include "ui/Widgets.h"

namespace pentagrit::ui
{
// One band: M/S/B, transient section (Attack / Sustain / Speed), saturation section (type, curve, Drive,
// Mix), output (Gain + level meter). In compact layout it also shows two From / To frequency fields.
class BandStrip final : public juce::Component
{
public:
    BandStrip(juce::AudioProcessorValueTreeState& apvts, MeterState& meters, int bandIndex);

    void applyTheme();
    void applyOptions(const UiOptions& o);
    // `numBands` = active bands; the From/To fields use it to know which edges are editable.
    void setNumBands(int numBands);
    void setDimmed(bool dim);
    // Called from the editor timer. Returns nothing; reads MeterState and (compact) the crossover values.
    void tick(float dtSec);

    int bandIndex() const noexcept { return index_; }
    // Link toggle in the header (see BandLink): the editor stores the state, the strip only shows it.
    std::function<void(bool)> onLinkToggled;
    void setLinked(bool linked);
    bool isLinked() const noexcept { return link_.getToggleState(); }
    // Header buttons, left to right: link, M, S, B (tests check that they do not overlap each other or the
    // "B1" label, which ends at kHeaderLabelRight).
    std::array<juce::Rectangle<int>, 4> headerButtonBounds() const noexcept
    {
        return {link_.getBounds(), mute_.getBounds(), solo_.getBounds(), bypass_.getBounds()};
    }
    static constexpr int kHeaderLabelRight = 36;
    bool isMuted() const noexcept;
    bool isSolo() const noexcept;
    // The Attack / Sustain / Speed knobs, left to right (their captions are checked by the tests).
    std::array<const LabelledKnob*, 3> transientKnobs() const noexcept
    {
        return {&attack_, &sustain_, &speed_};
    }

    const TransientIndicator& transientIndicator() const noexcept { return transient_; }

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    // Everything below the header lives here so that it can be dimmed as a whole.
    class Inner final : public juce::Component
    {
    public:
        void paint(juce::Graphics& g) override;
        std::array<juce::Rectangle<int>, 3> sections;
    };

    void commitFreq(bool isTo, const juce::String& text);
    void refreshFreqFields();
    std::array<float, 4> xoverValues() const;

    juce::AudioProcessorValueTreeState& apvts_;
    MeterState& meters_;
    int index_;
    int numBands_ = 3;
    bool compact_ = false;
    bool dimmed_ = false;

    juce::ToggleButton link_, mute_{"M"}, solo_{"S"}, bypass_{"B"};
    std::unique_ptr<juce::ButtonParameterAttachment> muteAtt_, soloAtt_, bypassAtt_;
    juce::Label from_, to_;
    juce::Label fromCaption_, toCaption_;

    Inner inner_;
    TransientIndicator transient_;
    LabelledKnob attack_, sustain_, speed_;
    SatTypeSelector sat_;
    CurvePreview curve_;
    LabelledKnob drive_, mix_;
    LabelledKnob gain_;
    LevelMeter meter_;
    LabelledKnob* knobs_[6];
    std::atomic<float>* muteRaw_;
    std::atomic<float>* soloRaw_;
    std::atomic<float>* driveRaw_;
    float lastDrive_ = -1.0f;
};
} // namespace pentagrit::ui
