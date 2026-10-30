// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Fonts.h"
#include "ui/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace pentagrit::ui
{
// Property keys (set on components through getProperties()).
namespace prop
{
inline const juce::Identifier bipolar{"pgBipolar"}; // Slider: arc starts at the centre
inline const juce::Identifier kind{"pgKind"}; // Button: "mute" | "solo" | "bypass" | "warn" | "gear" | "pill"
} // namespace prop

class PentagritLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit PentagritLookAndFeel(Style t = Style::studioDark);

    void setStyle(Style t);
    Style style() const noexcept { return style_; }
    const Palette& pal() const noexcept { return *pal_; }

    // Palette of the look-and-feel used by `c` (dark when it is not ours).
    static const Palette& paletteFor(const juce::Component& c);

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float sliderPos, float startAngle,
                          float endAngle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted,
                              bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    juce::Font getLabelFont(juce::Label&) override;

    static void drawGear(juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour);

private:
    void applyColours();

    Style style_;
    const Palette* pal_;
};
} // namespace pentagrit::ui
