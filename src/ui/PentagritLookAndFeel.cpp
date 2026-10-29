// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/PentagritLookAndFeel.h"

#include <cmath>

namespace pentagrit::ui
{
PentagritLookAndFeel::PentagritLookAndFeel(Theme t) : theme_(t), pal_(&palette(t))
{
    applyColours();
}

void PentagritLookAndFeel::setTheme(Theme t)
{
    theme_ = t;
    pal_ = &palette(t);
    applyColours();
}

const Palette& PentagritLookAndFeel::paletteFor(const juce::Component& c)
{
    if (auto* lf = dynamic_cast<PentagritLookAndFeel*>(&c.getLookAndFeel()))
        return lf->pal();
    return palette(Theme::dark);
}

void PentagritLookAndFeel::applyColours()
{
    const auto& p = *pal_;
    using juce::PopupMenu;
    setColour(PopupMenu::backgroundColourId, p.bg3);
    setColour(PopupMenu::textColourId, p.ink);
    setColour(PopupMenu::headerTextColourId, p.ink3);
    setColour(PopupMenu::highlightedBackgroundColourId, p.bg4);
    setColour(PopupMenu::highlightedTextColourId, p.ink);
    setColour(juce::ComboBox::backgroundColourId, p.bg2);
    setColour(juce::ComboBox::textColourId, p.ink);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::arrowColourId, p.ink2);
    setColour(juce::TextButton::buttonColourId, p.bg2);
    setColour(juce::TextButton::buttonOnColourId, p.ink);
    setColour(juce::TextButton::textColourOffId, p.ink2);
    setColour(juce::TextButton::textColourOnId, p.bg0);
    setColour(juce::Label::textColourId, p.ink2);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::backgroundColourId, p.bg0);
    setColour(juce::TextEditor::textColourId, p.ink);
    setColour(juce::TextEditor::outlineColourId, p.bg3);
    setColour(juce::TextEditor::focusedOutlineColourId, p.ink2);
    setColour(juce::TextEditor::highlightColourId, p.bg4);
    setColour(juce::TooltipWindow::backgroundColourId, p.bg3);
    setColour(juce::TooltipWindow::textColourId, p.ink);
    setColour(juce::TooltipWindow::outlineColourId, p.bg4);
    setColour(juce::AlertWindow::backgroundColourId, p.bg1);
    setColour(juce::AlertWindow::textColourId, p.ink);
    setColour(juce::AlertWindow::outlineColourId, p.bg3);
    setColour(juce::Slider::rotarySliderFillColourId, p.ink);
    setColour(juce::Slider::rotarySliderOutlineColourId, p.bg3);
    setColour(juce::Slider::thumbColourId, p.ink);
}

juce::Font PentagritLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return fonts::sans(11.0f, fonts::Weight::medium);
}
juce::Font PentagritLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return fonts::sans(11.5f, fonts::Weight::medium);
}
juce::Font PentagritLookAndFeel::getPopupMenuFont()
{
    return fonts::sans(12.0f, fonts::Weight::regular);
}
juce::Font PentagritLookAndFeel::getLabelFont(juce::Label&)
{
    return fonts::sans(11.0f, fonts::Weight::regular);
}

void PentagritLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.setColour(pal_->bg3);
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 4.0f);
}

void PentagritLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float start, float end, juce::Slider& s)
{
    const auto b = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                          static_cast<float>(h));
    const float d = juce::jmin(b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float trackW = 4.0f;
    const float r = d * 0.5f - trackW * 0.5f - 0.5f;
    const float angle = start + pos * (end - start);
    const bool enabled = s.isEnabled();

    juce::Path track;
    track.addCentredArc(c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour(pal_->bg3);
    g.strokePath(track,
                 juce::PathStrokeType(trackW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    const bool bipolar = static_cast<bool>(s.getProperties().getWithDefault(prop::bipolar, false));
    const float from = bipolar ? start + 0.5f * (end - start) : start;
    if (std::abs(angle - from) > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, r, r, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        g.setColour(s.findColour(juce::Slider::rotarySliderFillColourId).withAlpha(enabled ? 1.0f : 0.4f));
        g.strokePath(arc,
                     juce::PathStrokeType(trackW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    const float rb = r - trackW - 1.5f;
    if (rb > 3.0f)
    {
        g.setColour(pal_->bg2);
        g.fillEllipse(c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f);
        const float a = angle - juce::MathConstants<float>::halfPi;
        g.setColour(pal_->ink);
        g.drawLine(c.x + std::cos(a) * rb * 0.25f, c.y + std::sin(a) * rb * 0.25f,
                   c.x + std::cos(a) * rb * 0.95f, c.y + std::sin(a) * rb * 0.95f, 2.0f);
    }
    if (s.hasKeyboardFocus(false))
    {
        g.setColour(pal_->ink2);
        g.drawEllipse(c.x - d * 0.5f, c.y - d * 0.5f, d, d, 1.0f);
    }
}

void PentagritLookAndFeel::drawGear(juce::Graphics& g, juce::Rectangle<float> a, juce::Colour colour)
{
    const auto c = a.getCentre();
    const float r = a.getWidth() * 0.5f;
    juce::Path p;
    constexpr int teeth = 8;
    constexpr int steps = teeth * 4;
    for (int i = 0; i < steps; ++i)
    {
        // Pattern per tooth: outer, outer, inner, inner.
        const float rr = (i % 4 < 2) ? r : r * 0.76f;
        const float ang = juce::MathConstants<float>::twoPi * (static_cast<float>(i) - 0.5f) / steps;
        const juce::Point<float> pt(c.x + std::cos(ang) * rr, c.y + std::sin(ang) * rr);
        if (i == 0)
            p.startNewSubPath(pt);
        else
            p.lineTo(pt);
    }
    p.closeSubPath();
    p.addEllipse(c.x - r * 0.32f, c.y - r * 0.32f, r * 0.64f, r * 0.64f);
    p.setUsingNonZeroWinding(false);
    g.setColour(colour);
    g.fillPath(p);
}

void PentagritLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hl, bool down)
{
    const auto area = b.getLocalBounds().toFloat();
    const auto kind = b.getProperties().getWithDefault(prop::kind, "pill").toString();
    const bool on = b.getToggleState();
    const bool en = b.isEnabled();
    auto fillOn = pal_->ink;
    if (kind == "mute")
        fillOn = pal_->mute;
    else if (kind == "solo")
        fillOn = pal_->solo;
    else if (kind == "bypass")
        fillOn = pal_->bypass;
    else if (kind == "warn")
        fillOn = pal_->warn;

    juce::Colour bg = on ? fillOn : (hl || down ? pal_->bg3 : pal_->bg2);
    if (!en)
        bg = bg.withAlpha(0.5f);
    const bool small = kind == "mute" || kind == "solo" || kind == "bypass";
    const float radius = small ? 3.0f : 4.0f;
    g.setColour(bg);
    g.fillRoundedRectangle(area, radius);

    if (kind == "gear")
    {
        drawGear(g, area.withSizeKeepingCentre(16.0f, 16.0f), on ? pal_->bg0 : (hl ? pal_->ink : pal_->ink2));
    }
    else
    {
        g.setColour(en ? (on ? pal_->bg0 : (hl ? pal_->ink : pal_->ink2)) : pal_->ink3);
        g.setFont(small ? fonts::sans(10.5f, fonts::Weight::semiBold)
                        : fonts::sans(11.0f, fonts::Weight::semiBold, 0.06f));
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }
    if (b.hasKeyboardFocus(false))
    {
        g.setColour(pal_->ink2);
        g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    }
}

void PentagritLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                bool hl, bool down)
{
    const auto area = b.getLocalBounds().toFloat();
    g.setColour(b.getToggleState() ? pal_->ink : (down || hl ? pal_->bg3 : pal_->bg2));
    g.fillRoundedRectangle(area, 3.0f);
    if (b.hasKeyboardFocus(false))
    {
        g.setColour(pal_->ink2);
        g.drawRoundedRectangle(area.reduced(0.5f), 3.0f, 1.0f);
    }
}

void PentagritLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool hl, bool)
{
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(b.getToggleState() ? pal_->bg0 : (hl ? pal_->ink : pal_->ink2));
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

void PentagritLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int,
                                        juce::ComboBox& box)
{
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(area, 4.0f);
    juce::Path arrow;
    const float cx = area.getRight() - 11.0f, cy = area.getCentreY();
    arrow.addTriangle(cx - 3.5f, cy - 1.5f, cx + 3.5f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}
} // namespace pentagrit::ui
