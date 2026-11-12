// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/TopBar.h"

namespace pentagrit::ui
{
HistoryButton::HistoryButton(Kind kind, const juce::String& name) : juce::Button(name), kind_(kind)
{
    setTitle(name);
    setTooltip(name);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void HistoryButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto& p = pal(*this);
    const auto area = getLocalBounds().toFloat();
    const bool en = isEnabled();
    g.setColour((en && (highlighted || down) ? p.bg3 : p.bg2).withMultipliedAlpha(en ? 1.0f : 0.5f));
    g.fillRoundedRectangle(area, p.rButton);
    if (p.controlBorderW > 0.0f)
    {
        g.setColour(en && highlighted ? p.ink2 : p.bg4);
        g.drawRoundedRectangle(area.reduced(p.controlBorderW * 0.5f), p.rButton, p.controlBorderW);
    }

    // A half circle with an arrow head at its end. Redo is the mirror image of undo.
    constexpr float r = 4.6f;
    juce::Path arc;
    arc.addCentredArc(0.0f, 0.0f, r, r, 0.0f, 0.55f * juce::MathConstants<float>::pi,
                      -0.5f * juce::MathConstants<float>::pi, true);
    juce::Path head;
    head.addTriangle(-r - 2.6f, -0.4f, -r + 2.6f, -0.4f, -r, 3.0f);
    auto t = juce::AffineTransform::translation(area.getCentreX(), area.getCentreY() + 0.8f);
    if (kind_ == Kind::redo)
    {
        t = juce::AffineTransform::scale(-1.0f, 1.0f).followedBy(t);
    }
    g.setColour(en ? (highlighted || down ? p.ink : p.ink2) : p.ink3);
    g.strokePath(arc, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::butt),
                 t);
    g.fillPath(head, t);

    if (hasKeyboardFocus(false))
    {
        g.setColour(p.ink2);
        g.drawRoundedRectangle(area.reduced(0.5f), p.rButton, 1.0f);
    }
}

TopBar::TopBar(juce::AudioProcessorValueTreeState& apvts, MeterState& meters,
               std::function<double()> sampleRate)
    : meters_(meters), sampleRate_(std::move(sampleRate)),
      bands_(*apvts.getParameter(pid::numBands), juce::StringArray{"2", "3", "4", "5"}, 2, "Bands",
             "Number of crossover bands, 2 to 5. Not automatable."),
      os_(*apvts.getParameter(pid::oversampling), juce::StringArray{"Off", "2x", "4x", "8x"}, 0,
          "Oversampling",
          "Oversampling of the whole processing chain: Off, 2x, 4x or 8x. Less aliasing from the "
          "saturators, more latency and CPU. Not automatable.")
{
    setTitle("Top bar");
    addAndMakeVisible(presetSlot_);
    presetSlot_.setTitle("Preset slot");
    addAndMakeVisible(bands_);
    addAndMakeVisible(os_);
    undo_.setTooltip("Undo the last parameter change (Ctrl/Cmd+Z).");
    redo_.setTooltip("Redo the change you undid (Ctrl/Cmd+Shift+Z or Ctrl+Y).");
    for (auto* b : {&undo_, &redo_})
    {
        b->setEnabled(false);
        addAndMakeVisible(*b);
    }
    undo_.onClick = [this]
    {
        if (onUndo)
        {
            onUndo();
        }
    };
    redo_.onClick = [this]
    {
        if (onRedo)
        {
            onRedo();
        }
    };

    autoGain_.getProperties().set(prop::kind, "pill");
    autoGain_.setTitle("Auto-gain");
    autoGain_.setTooltip(
        "Auto-gain: keeps the output level matched to the input level (up to +-24 dB of correction).");
    bypass_.getProperties().set(prop::kind, "warn");
    bypass_.setTitle("Bypass");
    bypass_.setTooltip("Bypass the whole plugin. The bypass is delay-compensated and fades over 20 ms.");
    gear_.getProperties().set(prop::kind, "gear");
    gear_.setTitle("Settings");
    gear_.setTooltip("Open the settings: interface, audio, presets, and about with the user guide.");
    gear_.setClickingTogglesState(false);
    gear_.onClick = [this]
    {
        if (onSettingsClicked)
        {
            onSettingsClicked();
        }
    };
    for (auto* b : {&autoGain_, &bypass_, &gear_})
    {
        b->setWantsKeyboardFocus(true);
        addAndMakeVisible(*b);
    }
    autoGainAtt_ = std::make_unique<juce::ButtonParameterAttachment>(*apvts.getParameter(pid::autoGain),
                                                                     autoGain_, nullptr);
    bypassAtt_ =
        std::make_unique<juce::ButtonParameterAttachment>(*apvts.getParameter(pid::bypass), bypass_, nullptr);
    tick();
}

void TopBar::setHistoryState(bool canUndo, bool canRedo)
{
    undo_.setEnabled(canUndo);
    redo_.setEnabled(canRedo);
}

void TopBar::applyTheme()
{
    undo_.repaint();
    redo_.repaint();
    repaint();
}

void TopBar::tick()
{
    const int lat = meters_.latencySamples.load(std::memory_order_relaxed);
    const double sr = sampleRate_ ? sampleRate_() : 0.0;
    if (lat == lastLatency_ && sr == lastRate_)
    {
        return;
    }
    lastLatency_ = lat;
    lastRate_ = sr;
    // One decimal below 100 ms, none above, so the text stays short (at most "5.3 ms  1009 smp" in practice).
    const double ms = sr > 0.0 ? lat * 1000.0 / sr : 0.0;
    latencyText_ = sr > 0.0 ? juce::String(ms, ms >= 100.0 ? 0 : 1) + " ms  " + juce::String(lat) + " smp"
                            : juce::String(lat) + " smp";
    repaint(latencyArea_);
}

juce::String TopBar::getTooltip()
{
    if (latencyArea_.contains(getMouseXYRelative()))
    {
        return "Latency reported to the host, compensated by it. It grows with oversampling and lookahead.";
    }
    return {};
}

void TopBar::resized()
{
    auto b = getLocalBounds().reduced(10, 0);
    const int h = 26;
    auto centreV = [&](juce::Rectangle<int> r) { return r.withSizeKeepingCentre(r.getWidth(), h); };

    // Tight sizes so that the preset bar (about 324 px, 300 at the least) fits next to the selectors and the
    // undo / redo buttons at the 1100 px base width.
    constexpr int gap = 4;
    b.removeFromLeft(36 + 8 + 86 + 8); // logo + wordmark

    gear_.setBounds(juce::Rectangle<int>(28, 28).withCentre({b.getRight() - 14, getHeight() / 2}));
    b.removeFromRight(28 + gap);
    bypass_.setBounds(centreV(b.removeFromRight(62)));
    b.removeFromRight(gap);
    autoGain_.setBounds(centreV(b.removeFromRight(74)));
    b.removeFromRight(gap);
    latencyArea_ = b.removeFromRight(kLatencyW);
    b.removeFromRight(gap);
    os_.setBounds(centreV(b.removeFromRight(4 * 28 + 4)));
    osLabelArea_ = b.removeFromRight(22);
    b.removeFromRight(gap);
    bands_.setBounds(centreV(b.removeFromRight(4 * 26 + 4)));
    bandsLabelArea_ = b.removeFromRight(34);
    b.removeFromRight(gap);
    redo_.setBounds(centreV(b.removeFromRight(20)));
    b.removeFromRight(2);
    undo_.setBounds(centreV(b.removeFromRight(20)));
    b.removeFromRight(gap);
    presetSlot_.setBounds(b.withSizeKeepingCentre(juce::jmax(0, b.getWidth()), 30));
}

juce::Font TopBar::latencyFont() const
{
    // Normal size, shrunk (never below 8.5 px) when the text would not fit its area with the side padding.
    const float avail = static_cast<float>(latencyArea_.getWidth() - 2 * kLatencyPad);
    auto widthOf = [this](const juce::Font& f)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(f, latencyText_, 0.0f, 0.0f);
        return ga.getBoundingBox(0, -1, true).getWidth();
    };
    auto font = fonts::mono(10.5f, fonts::Weight::regular);
    for (float size = 10.5f; latencyArea_.getWidth() > 0 && widthOf(font) > avail && size > 8.5f;
         size -= 0.25f)
    {
        font = fonts::mono(size, fonts::Weight::regular);
    }
    return font;
}

float TopBar::latencyTextWidth() const
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(latencyFont(), latencyText_, 0.0f, 0.0f);
    return ga.getBoundingBox(0, -1, true).getWidth();
}

void TopBar::drawLogoMark(juce::Graphics& g, juce::Rectangle<float> area, const Palette& p)
{
    // Five wedges of the brand mark (viewBox 256, polygons from brand/icon.svg).
    struct Poly
    {
        float pts[6];
    };
    static constexpr Poly polys[5] = {{{132.1f, 128.3f, 132.1f, 24.3f, 231.0f, 96.2f}},
                                      {{134.7f, 136.2f, 233.6f, 104.0f, 195.8f, 220.3f}},
                                      {{128.0f, 141.0f, 189.1f, 225.1f, 66.9f, 225.1f}},
                                      {{121.3f, 136.2f, 60.2f, 220.3f, 22.4f, 104.0f}},
                                      {{123.9f, 128.3f, 25.0f, 96.2f, 123.9f, 24.3f}}};
    constexpr float x0 = 22.4f, y0 = 24.3f, sw = 233.6f - 22.4f, sh = 225.1f - 24.3f;
    const float s = juce::jmin(area.getWidth() / sw, area.getHeight() / sh);
    const float ox = area.getCentreX() - sw * s * 0.5f - x0 * s;
    const float oy = area.getCentreY() - sh * s * 0.5f - y0 * s;
    for (int i = 0; i < 5; ++i)
    {
        juce::Path path;
        const auto& q = polys[i].pts;
        path.startNewSubPath(ox + q[0] * s, oy + q[1] * s);
        path.lineTo(ox + q[2] * s, oy + q[3] * s);
        path.lineTo(ox + q[4] * s, oy + q[5] * s);
        path.closeSubPath();
        g.setColour(p.band[static_cast<size_t>(i)]);
        g.fillPath(path);
        g.strokePath(path, juce::PathStrokeType(3.0f * s, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
}

void TopBar::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    fillPanel(g, getLocalBounds().toFloat(), p);

    drawLogoMark(
        g, juce::Rectangle<float>(10.0f, static_cast<float>(getHeight()) * 0.5f - 15.0f, 32.0f, 30.0f), p);
    g.setColour(p.ink);
    // Wide display faces (Fraunces, Chakra Petch) are scaled down to fit the 92 px slot before the preset
    // bar.
    constexpr float kWordmarkW = 92.0f;
    auto wordmark = fonts::display(13.0f, fonts::Weight::semiBold, 0.14f);
    juce::GlyphArrangement ga;
    ga.addLineOfText(wordmark, "PENTAGRIT", 0.0f, 0.0f);
    const float textW = ga.getBoundingBox(0, -1, true).getWidth();
    if (textW > kWordmarkW)
    {
        wordmark = fonts::display(13.0f * kWordmarkW / textW, fonts::Weight::semiBold, 0.14f);
    }
    g.setFont(wordmark);
    g.drawText("PENTAGRIT", 54, 0, 94, getHeight(), juce::Justification::centredLeft, false);

    g.setColour(p.ink3);
    g.setFont(fonts::sans(10.0f, fonts::Weight::medium, 0.08f));
    g.drawText("BANDS", bandsLabelArea_, juce::Justification::centredRight, false);
    g.drawText("OS", osLabelArea_, juce::Justification::centredRight, false);

    g.setColour(p.ink2);
    g.setFont(latencyFont());
    g.drawText(latencyText_, latencyArea_.reduced(kLatencyPad, 0), juce::Justification::centredRight, false);
}
} // namespace pentagrit::ui
