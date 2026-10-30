// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/GlobalPanel.h"

namespace pentagrit::ui
{
using Fmt = LabelledKnob::Fmt;

GlobalPanel::GlobalPanel(juce::AudioProcessorValueTreeState& apvts, MeterState& meters)
    : meters_(meters), autoGainOn_(apvts.getRawParameterValue(pid::autoGain)),
      in_(apvts, pid::inGain, "In", "Input gain, before the crossover", Fmt::dbSignedUnit, true, 44),
      out_(apvts, pid::outGain, "Out", "Output gain, after the global mix", Fmt::dbSignedUnit, true, 44),
      mix_(apvts, pid::mix, "Mix", "Global dry/wet (phase-matched dry)", Fmt::percent, false, 44)
{
    setTitle("Master");
    for (auto* c : std::initializer_list<juce::Component*>{&in_, &out_, &mix_, &inMeter_, &outMeter_})
        addAndMakeVisible(*c);
    inMeter_.setTooltip("Input peak level");
    inMeter_.setTitle("Input level");
    outMeter_.setTooltip("Output peak level");
    outMeter_.setTitle("Output level");
    applyTheme();
}

void GlobalPanel::applyTheme()
{
    const auto& p = pal(*this);
    for (auto* k : {&in_, &out_, &mix_})
        k->setColour(p.ink);
    inMeter_.setBarColour(p.ink);
    outMeter_.setBarColour(p.ink);
    repaint();
}

void GlobalPanel::applyOptions(const UiOptions& o)
{
    for (auto* k : {&in_, &out_, &mix_})
        k->applyOptions(o);
    for (auto* m : {&inMeter_, &outMeter_})
        m->setBallistics(o.meterHoldSec, o.meterReleaseDbPerSec);
}

void GlobalPanel::tick(float dt)
{
    inMeter_.update(meters_.inPeakDb.load(std::memory_order_relaxed), dt);
    outMeter_.update(meters_.outPeakDb.load(std::memory_order_relaxed), dt);

    juce::String t = "off";
    if (autoGainOn_->load(std::memory_order_relaxed) >= 0.5f)
    {
        const float g = meters_.autoGainDb.load(std::memory_order_relaxed);
        t = (g > 0.049f ? "+" : "") + juce::String(g, 1) + " dB";
    }
    if (t != agText_)
    {
        agText_ = t;
        repaint();
    }
}

void GlobalPanel::resized()
{
    auto b = getLocalBounds().reduced(8, 8);
    b.removeFromTop(16); // heading
    auto cols = b.removeFromTop(12 + 46 + 14 + 6 + 140);
    const int cw = cols.getWidth() / 2;
    auto c1 = cols.removeFromLeft(cw), c2 = cols;
    in_.setBounds(c1.removeFromTop(72));
    out_.setBounds(c2.removeFromTop(72));
    c1.removeFromTop(6);
    c2.removeFromTop(6);
    inMeter_.setBounds(juce::Rectangle<int>(12, 140).withCentre({c1.getCentreX(), c1.getY() + 70}));
    outMeter_.setBounds(juce::Rectangle<int>(12, 140).withCentre({c2.getCentreX(), c2.getY() + 70}));
    b.removeFromTop(6);
    mix_.setBounds(b.removeFromTop(72).withSizeKeepingCentre(juce::jmin(b.getWidth(), 90), 72));
}

void GlobalPanel::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    fillPanel(g, getLocalBounds().toFloat(), p);
    g.setColour(p.ink3);
    g.setFont(fonts::sans(10.0f, fonts::Weight::medium, 0.08f));
    g.drawText("MASTER", 12, 8, getWidth() - 24, 16, juce::Justification::centredLeft, false);

    // Auto-gain readout bar at the bottom.
    auto r = getLocalBounds().reduced(8).removeFromBottom(26).toFloat();
    g.setColour(p.bg0);
    g.fillRoundedRectangle(r, p.rWell);
    g.setColour(p.ink3);
    g.setFont(fonts::sans(10.0f, fonts::Weight::medium, 0.08f));
    g.drawText("AUTO-GAIN", r.reduced(8, 0).toNearestInt(), juce::Justification::centredLeft, false);
    g.setColour(p.ink2);
    g.setFont(fonts::mono(10.5f, fonts::Weight::regular));
    g.drawText(agText_, r.reduced(8, 0).toNearestInt(), juce::Justification::centredRight, false);
}
} // namespace pentagrit::ui
