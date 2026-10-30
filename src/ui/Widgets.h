// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/PentagritLookAndFeel.h"
#include "ui/UiOptions.h"
#include <array>
#include <cmath>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace pentagrit::ui
{
inline const Palette& pal(const juce::Component& c)
{
    return PentagritLookAndFeel::paletteFor(c);
}

// Rounds v to the nearest multiple of step; step <= 0 means continuous (v unchanged).
inline double quantiseToStep(double v, double step)
{
    return step > 0.0 ? std::round(v / step) * step : v;
}

// Slider with double-click to type a value, Ctrl/Cmd+click to reset, optional drag step and 2 % wheel steps
class KnobSlider final : public juce::Slider
{
public:
    KnobSlider();

    std::function<void()> onTypeRequest;
    double dragStep = 0.0; // 0 = continuous; Shift (fine adjustment) ignores it

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

protected:
    double snapValue(double attempted, DragMode) override;

private:
    bool resetClick_ = false;
};

// Knob with label above and value below
class LabelledKnob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Fmt
    {
        dbSigned, // "+3.0"
        dbUnit,   // "-2.5 dB"
        dbSignedUnit,
        percent, // 0..1 -> "75%"
        speed    // 0..1 -> "x0.25..x4"
    };

    LabelledKnob(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                 const juce::String& label, const juce::String& hint, Fmt fmt, bool bipolar, int knobSize);

    void setColour(juce::Colour c);
    void applyOptions(const UiOptions& o);
    KnobSlider& slider() noexcept { return slider_; }
    juce::String valueText() const { return valueText_; }
    // Applies typed text as a value (clamped to the range, never rounded to the value step).
    void commitText(const juce::String& text);
    static bool isDbFormat(Fmt f) noexcept
    {
        return f == Fmt::dbSigned || f == Fmt::dbUnit || f == Fmt::dbSignedUnit;
    }

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void updateText();
    void beginTyping();
    void endTyping(bool commit);
    double parse(const juce::String&) const;

    KnobSlider slider_;
    std::unique_ptr<juce::SliderParameterAttachment> attachment_;
    std::unique_ptr<juce::TextEditor> editor_;
    juce::String label_, valueText_;
    Fmt fmt_;
    int knobSize_;
};

// Vertical level meter (no scale): ballistics, peak hold, clip colour
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float floorDb = -60.0f;

    void setBarColour(juce::Colour c)
    {
        colour_ = c;
        repaint();
    }
    void setBallistics(float holdSec, float releaseDbPerSec)
    {
        holdSec_ = holdSec;
        releaseDbPerSec_ = releaseDbPerSec;
    }
    // Feed the latest peak (dBFS) and the elapsed time since the previous call.
    void update(float peakDb, float dtSec);
    float displayDb() const noexcept { return levelDb_; }

    void paint(juce::Graphics&) override;

private:
    juce::Colour colour_{juce::Colours::white};
    float levelDb_ = -100.0f, holdDb_ = -100.0f, holdAge_ = 0.0f;
    float holdSec_ = 1.0f, releaseDbPerSec_ = 20.0f;
};

// Segmented selector bound to an int/choice parameter
class SegmentedSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentedSelector(juce::RangedAudioParameter& param, const juce::StringArray& labels, int firstValue,
                      const juce::String& title, const juce::String& hint);

    int selectedIndex() const noexcept { return selected_; }
    void select(int index); // user action (gesture)

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    int indexAt(juce::Point<int>) const;

    juce::StringArray labels_;
    int first_;
    int selected_ = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
};

// Saturation type selector: < name >
class SatTypeSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SatTypeSelector(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                    const juce::String& title);

    std::function<void(int)> onTypeChanged;
    int type() const noexcept { return type_; }

    void resized() override;

private:
    void step(int d);

    juce::AudioParameterChoice* param_ = nullptr;
    juce::TextButton prev_{juce::String(juce::CharPointer_UTF8("\xe2\x80\xb9"))}, name_,
        next_{juce::String(juce::CharPointer_UTF8("\xe2\x80\xba"))};
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    int type_ = 0;
};

// Transfer-curve preview (uses Saturator::shape)
class CurvePreview final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setBandColour(juce::Colour c)
    {
        colour_ = c;
        repaint();
    }
    void setType(int satType);
    void setDriveDb(float db);

    void resized() override { rebuild(); }
    void paint(juce::Graphics&) override;

private:
    void rebuild();

    juce::Colour colour_{juce::Colours::white};
    int type_ = 0;
    float driveDb_ = 6.0f;
    juce::Path curve_;
};

// Centre-zero transient gain indicator (+-24 dB full scale)
class TransientIndicator final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setBandColour(juce::Colour c)
    {
        colour_ = c;
        repaint();
    }
    void setGainDb(float db);

    void paint(juce::Graphics&) override;

private:
    juce::Colour colour_{juce::Colours::white};
    float gainDb_ = 0.0f;
};

// Plain container that stretches its (single) child over its bounds. Used for the preset slot.
class SlotComponent final : public juce::Component
{
public:
    void resized() override
    {
        for (auto* c : getChildren())
            c->setBounds(getLocalBounds());
    }
};

// Helper shared by the crossover display and the compact From/To fields.
namespace xover
{
inline constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
inline const float ratio = std::pow(2.0f, 1.0f / 3.0f); // minimum distance, 1/3 octave

// Allowed range for crossover `index` (0..3) given the current values and the number of bands.
juce::Range<float> allowedRange(int index, const std::array<float, 4>& hz, int numBands);
float clamp(int index, float hz, const std::array<float, 4>& hz4, int numBands);
// Parses "1200", "1.2k", "1.2 kHz", "80hz". Returns false when no number was found.
bool parseHz(const juce::String& text, float& out);
juce::String format(float hz);      // "120 Hz" / "1.20 kHz"
juce::String formatShort(float hz); // "120" / "1.2k"
} // namespace xover
} // namespace pentagrit::ui
