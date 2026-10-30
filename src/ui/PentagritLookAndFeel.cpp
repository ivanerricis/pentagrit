// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/PentagritLookAndFeel.h"

#include <cmath>

namespace pentagrit::ui
{
PentagritLookAndFeel::PentagritLookAndFeel(Style t) : style_(t), pal_(&palette(t))
{
    fonts::setStyle(t);
    applyColours();
}

void PentagritLookAndFeel::setStyle(Style t)
{
    style_ = t;
    pal_ = &palette(t);
    fonts::setStyle(t);
    applyColours();
}

const Palette& PentagritLookAndFeel::paletteFor(const juce::Component& c)
{
    if (auto* lf = dynamic_cast<PentagritLookAndFeel*>(&c.getLookAndFeel()))
        return lf->pal();
    return palette(Style::studioDark);
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
    setColour(juce::TextButton::buttonOnColourId, p.act);
    setColour(juce::TextButton::textColourOffId, p.ink2);
    setColour(juce::TextButton::textColourOnId, p.onAct);
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
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), pal_->rButton);
    if (pal_->controlBorderW > 0.0f)
    {
        g.setColour(pal_->bg4);
        g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(w) - 1.0f, static_cast<float>(h) - 1.0f,
                               pal_->rButton, pal_->controlBorderW);
    }
}

void PentagritLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float start, float end, juce::Slider& s)
{
    const auto b = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                          static_cast<float>(h));
    const float d = juce::jmin(b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const auto& p = *pal_;
    const float trackW = p.knobTrackW;
    const float arcW = p.knobArcW;
    const float maxW = juce::jmax(trackW, arcW);
    const float r = d * 0.5f - maxW * 0.5f - 0.5f;
    const float angle = start + pos * (end - start);
    const bool enabled = s.isEnabled();
    const auto cap =
        p.knob == KnobShape::thumbDot ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt;

    juce::Path track;
    track.addCentredArc(c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour(p.bg3);
    g.strokePath(track, juce::PathStrokeType(trackW, juce::PathStrokeType::curved, cap));

    const bool bipolar = static_cast<bool>(s.getProperties().getWithDefault(prop::bipolar, false));
    const float from = bipolar ? start + 0.5f * (end - start) : start;
    if (std::abs(angle - from) > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, r, r, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        g.setColour(s.findColour(juce::Slider::rotarySliderFillColourId).withAlpha(enabled ? 1.0f : 0.4f));
        g.strokePath(arc, juce::PathStrokeType(arcW, juce::PathStrokeType::curved, cap));
    }

    const float rb = r - maxW - 1.5f; // radius of the inner body
    const float a = angle - juce::MathConstants<float>::halfPi;
    const float ca = std::cos(a), sa = std::sin(a);
    switch (p.knob)
    {
        case KnobShape::line:
            if (rb > 3.0f)
            {
                g.setColour(p.bg2);
                g.fillEllipse(c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f);
                g.setColour(p.ink);
                g.drawLine(c.x + ca * rb * 0.25f, c.y + sa * rb * 0.25f, c.x + ca * rb * 0.95f,
                           c.y + sa * rb * 0.95f, 2.0f);
            }
            break;
        case KnobShape::bigCap:
            if (rb > 3.0f)
            {
                g.setColour(p.ink);
                g.fillEllipse(c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f);
                g.setColour(p.bg0);
                g.drawLine(c.x + ca * rb * 0.3f, c.y + sa * rb * 0.3f, c.x + ca * rb * 0.9f,
                           c.y + sa * rb * 0.9f, 2.0f);
            }
            break;
        case KnobShape::ring:
            if (rb > 3.0f)
            {
                const float rr = rb * 0.8f;
                g.setColour(p.bg4);
                g.drawEllipse(c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 1.2f);
                g.setColour(p.ink);
                g.drawLine(c.x + ca * rr * 0.4f, c.y + sa * rr * 0.4f, c.x + ca * rr, c.y + sa * rr, 1.6f);
            }
            break;
        case KnobShape::whiteCap:
            if (rb > 3.0f)
            {
                g.setColour(p.bg1);
                g.fillEllipse(c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f);
                g.setColour(p.bg4);
                g.drawEllipse(c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f, 1.0f);
                g.setColour(p.act);
                g.drawLine(c.x + ca * rb * 0.25f, c.y + sa * rb * 0.25f, c.x + ca * rb * 0.9f,
                           c.y + sa * rb * 0.9f, 2.2f);
            }
            break;
        case KnobShape::heavy:
            if (rb > 3.0f)
            {
                g.setColour(p.bg4);
                g.fillEllipse(c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f);
                // square-ended pointer: a rotated rectangle
                juce::Path ptr;
                ptr.addRectangle(-1.8f, -rb * 0.95f, 3.6f, rb * 0.7f);
                g.setColour(p.ink);
                g.fillPath(ptr, juce::AffineTransform::rotation(angle).translated(c.x, c.y));
            }
            break;
        case KnobShape::thumbDot:
            if (rb > 3.0f)
            {
                g.setColour(p.bg0);
                g.fillEllipse(c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f);
            }
            {
                const float tx = c.x + std::cos(a) * r, ty = c.y + std::sin(a) * r;
                g.setColour(p.ink);
                g.fillEllipse(tx - 3.5f, ty - 3.5f, 7.0f, 7.0f);
            }
            break;
    }
    if (s.hasKeyboardFocus(false))
    {
        g.setColour(p.ink2);
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
    auto fillOn = pal_->act;
    bool onBand = false;
    if (kind == "mute")
        fillOn = pal_->mute, onBand = true;
    else if (kind == "solo")
        fillOn = pal_->solo, onBand = true;
    else if (kind == "bypass")
        fillOn = pal_->bypass, onBand = true;
    else if (kind == "warn")
        fillOn = pal_->warn, onBand = true;

    juce::Colour bg = on ? fillOn : (hl || down ? pal_->bg3 : pal_->bg2);
    if (!en)
        bg = bg.withAlpha(0.5f);
    const bool small = kind == "mute" || kind == "solo" || kind == "bypass";
    const float radius = small ? pal_->rSmall : pal_->rButton;
    const auto onColour = onBand ? pal_->onBand : pal_->onAct;
    g.setColour(bg);
    g.fillRoundedRectangle(area, radius);
    if (pal_->controlBorderW > 0.0f && !on)
    {
        g.setColour(hl ? pal_->ink2 : pal_->bg4);
        g.drawRoundedRectangle(area.reduced(pal_->controlBorderW * 0.5f), radius, pal_->controlBorderW);
    }

    if (kind == "gear")
    {
        drawGear(g, area.withSizeKeepingCentre(16.0f, 16.0f), on ? onColour : (hl ? pal_->ink : pal_->ink2));
    }
    else
    {
        g.setColour(en ? (on ? onColour : (hl ? pal_->ink : pal_->ink2)) : pal_->ink3);
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
    g.setColour(b.getToggleState() ? pal_->act : (down || hl ? pal_->bg3 : pal_->bg2));
    g.fillRoundedRectangle(area, pal_->rSmall);
    if (pal_->controlBorderW > 0.0f && !b.getToggleState())
    {
        g.setColour(hl ? pal_->ink2 : pal_->bg4);
        g.drawRoundedRectangle(area.reduced(pal_->controlBorderW * 0.5f), pal_->rSmall, pal_->controlBorderW);
    }
    if (b.hasKeyboardFocus(false))
    {
        g.setColour(pal_->ink2);
        g.drawRoundedRectangle(area.reduced(0.5f), pal_->rSmall, 1.0f);
    }
}

void PentagritLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool hl, bool)
{
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(b.getToggleState() ? pal_->onAct : (hl ? pal_->ink : pal_->ink2));
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

void PentagritLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int,
                                        juce::ComboBox& box)
{
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(area, pal_->rButton);
    if (pal_->controlBorderW > 0.0f)
    {
        g.setColour(pal_->bg4);
        g.drawRoundedRectangle(area.reduced(pal_->controlBorderW * 0.5f), pal_->rButton,
                               pal_->controlBorderW);
    }
    juce::Path arrow;
    const float cx = area.getRight() - 11.0f, cy = area.getCentreY();
    arrow.addTriangle(cx - 3.5f, cy - 1.5f, cx + 3.5f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}
} // namespace pentagrit::ui
