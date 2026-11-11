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
        t = juce::AffineTransform::scale(-1.0f, 1.0f).followedBy(t);
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
             "Number of crossover bands (2 to 5)"),
      os_(*apvts.getParameter(pid::oversampling), juce::StringArray{"Off", "2x", "4x", "8x"}, 0,
          "Oversampling", "Oversampling factor of the whole processing chain (adds latency)")
{
    setTitle("Top bar");
    addAndMakeVisible(presetSlot_);
    presetSlot_.setTitle("Preset slot");
    addAndMakeVisible(bands_);
    addAndMakeVisible(os_);
    for (auto* b : {&undo_, &redo_})
    {
        b->setEnabled(false);
        addAndMakeVisible(*b);
    }
    undo_.onClick = [this]
    {
        if (onUndo)
            onUndo();
    };
    redo_.onClick = [this]
    {
        if (onRedo)
            onRedo();
    };

    autoGain_.getProperties().set(prop::kind, "pill");
    autoGain_.setTitle("Auto-gain");
    autoGain_.setTooltip("Auto-gain: keeps the output level matched to the input");
    bypass_.getProperties().set(prop::kind, "warn");
    bypass_.setTitle("Bypass");
    bypass_.setTooltip("Global bypass (delay-compensated, 20 ms crossfade)");
    gear_.getProperties().set(prop::kind, "gear");
    gear_.setTitle("Settings");
    gear_.setTooltip("Settings: interface, audio, presets, about");
    gear_.setClickingTogglesState(false);
    gear_.onClick = [this]
    {
        if (onSettingsClicked)
            onSettingsClicked();
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
        return;
    lastLatency_ = lat;
    lastRate_ = sr;
    latencyText_ = sr > 0.0 ? juce::String(lat * 1000.0 / sr, 2) + " ms  " + juce::String(lat) + " smp"
                            : juce::String(lat) + " smp";
    repaint(latencyArea_);
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
    latencyArea_ = b.removeFromRight(112);
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
        wordmark = fonts::display(13.0f * kWordmarkW / textW, fonts::Weight::semiBold, 0.14f);
    g.setFont(wordmark);
    g.drawText("PENTAGRIT", 54, 0, 94, getHeight(), juce::Justification::centredLeft, false);

    g.setColour(p.ink3);
    g.setFont(fonts::sans(10.0f, fonts::Weight::medium, 0.08f));
    g.drawText("BANDS", bandsLabelArea_, juce::Justification::centredRight, false);
    g.drawText("OS", osLabelArea_, juce::Justification::centredRight, false);

    g.setColour(p.ink2);
    g.setFont(fonts::mono(10.5f, fonts::Weight::regular));
    g.drawText(latencyText_, latencyArea_, juce::Justification::centredRight, false);
}
} // namespace pentagrit::ui
