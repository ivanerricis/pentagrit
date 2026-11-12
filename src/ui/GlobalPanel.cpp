// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/GlobalPanel.h"

namespace pentagrit::ui
{
using Fmt = LabelledKnob::Fmt;

GlobalPanel::GlobalPanel(PentagritAudioProcessor& processor)
    : processor_(processor), meters_(processor.getMeters()),
      autoGainOn_(processor.getAPVTS().getRawParameterValue(pid::autoGain)),
      in_(processor.getAPVTS(), pid::inGain, "In", "Input gain, before the crossover.", Fmt::dbSignedUnit,
          true, 44),
      out_(processor.getAPVTS(), pid::outGain, "Out", "Output gain, after the global mix.", Fmt::dbSignedUnit,
           true, 44),
      mix_(processor.getAPVTS(), pid::mix, "Mix",
           "Global dry/wet mix. The dry path is phase-matched, so partial mixes stay clean.", Fmt::percent,
           false, 44)
{
    auto& apvts = processor.getAPVTS();
    setTitle("Master");
    for (auto* c : std::initializer_list<juce::Component*>{&in_, &out_, &mix_, &inMeter_, &outMeter_})
    {
        addAndMakeVisible(*c);
    }
    driveComp_.getProperties().set(prop::kind, "pill");
    driveComp_.setTitle("Drive compensation");
    driveComp_.setTooltip(
        "Drive compensation: keeps the level of quiet signals constant while you raise the drive.");
    driveComp_.setWantsKeyboardFocus(true);
    addAndMakeVisible(driveComp_);
    driveCompAtt_ = std::make_unique<juce::ButtonParameterAttachment>(*apvts.getParameter(pid::driveComp),
                                                                      driveComp_, nullptr);
    delta_.getProperties().set(prop::kind, "pill");
    delta_.setTitle("Delta monitoring");
    delta_.setTooltip(
        "Delta: listen only to what the plugin changes (processed minus dry), to hear what you add "
        "or remove. Not saved with the project; Bypass wins.");
    delta_.setWantsKeyboardFocus(true);
    delta_.onClick = [this]
    {
        processor_.setDeltaMonitoring(delta_.getToggleState());
        syncMonitor();
    };
    matchBypass_.getProperties().set(prop::kind, "pill");
    matchBypass_.setTitle("Match bypass level");
    matchBypass_.setTooltip(
        "Match bypass: while on, Bypass is scaled to the level the plugin output had, so "
        "A/B compares tone and not loudness. Live playback only (offline renders keep the "
        "exact bypass). Kept in your settings, not in the project.");
    matchBypass_.setWantsKeyboardFocus(true);
    matchBypass_.onClick = [this]
    {
        auto s = processor_.getSettings().get();
        s.levelMatchBypass = matchBypass_.getToggleState();
        processor_.getSettings().set(s); // the processor mirrors it for the audio thread
        syncMonitor();
    };
    addAndMakeVisible(delta_);
    addAndMakeVisible(matchBypass_);
    inMeter_.setTooltip("Input peak level in dBFS, with peak hold.");
    inMeter_.setTitle("Input level");
    outMeter_.setTooltip("Output peak level in dBFS, with peak hold. Tinted while Delta is on.");
    outMeter_.setTitle("Output level");
    applyTheme();
}

void GlobalPanel::applyTheme()
{
    const auto& p = pal(*this);
    for (auto* k : {&in_, &out_, &mix_})
    {
        k->setColour(p.ink);
    }
    inMeter_.setBarColour(p.ink);
    outMeter_.setBarColour(deltaShown_ ? p.act : p.ink);
    repaint();
}

void GlobalPanel::applyOptions(const UiOptions& o)
{
    for (auto* k : {&in_, &out_, &mix_})
    {
        k->applyOptions(o);
    }
    for (auto* m : {&inMeter_, &outMeter_})
    {
        m->setBallistics(o.meterHoldSec, o.meterReleaseDbPerSec);
    }
}

void GlobalPanel::syncMonitor()
{
    const bool d = processor_.isDeltaMonitoring();
    const bool m = processor_.getSettings().get().levelMatchBypass;
    delta_.setToggleState(d, juce::dontSendNotification);
    matchBypass_.setToggleState(m, juce::dontSendNotification);
    if (d != deltaShown_)
    {
        deltaShown_ = d;
        outMeter_.setBarColour(d ? pal(*this).act : pal(*this).ink); // "DELTA" tint
        repaint();
    }
    juce::String t;
    if (m)
    {
        const float db = meters_.bypassMatchDb.load(std::memory_order_relaxed);
        t = (db > 0.049f ? "+" : "") + juce::String(db, 1) + " dB";
    }
    if (t != matchText_)
    {
        matchText_ = t;
        repaint(monitorHead_);
    }
}

void GlobalPanel::tick(float dt)
{
    syncMonitor();
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
    // 128 px meters; the last 12 px of the column hold the DELTA caption.
    inMeter_.setBounds(juce::Rectangle<int>(12, 128).withCentre({c1.getCentreX(), c1.getY() + 64}));
    outMeter_.setBounds(juce::Rectangle<int>(12, 128).withCentre({c2.getCentreX(), c2.getY() + 64}));
    deltaCaption_ = juce::Rectangle<int>(c2.getX(), c2.getY() + 128, c2.getWidth(), 12);
    b.removeFromTop(4);
    mix_.setBounds(b.removeFromTop(72).withSizeKeepingCentre(juce::jmin(b.getWidth(), 90), 72));
    // Drive compensation switch, centred between the mix knob and the auto-gain readout.
    b.removeFromBottom(26 + 4);
    // MONITOR row at the bottom of what is left: heading with the match readout, then the two toggles.
    auto toggles = b.removeFromBottom(20);
    monitorHead_ = b.removeFromBottom(12);
    delta_.setBounds(toggles.removeFromLeft(62)); // "MATCH BYPASS" needs the wider one
    toggles.removeFromLeft(4);
    matchBypass_.setBounds(toggles);
    driveComp_.setBounds(
        b.withSizeKeepingCentre(juce::jmin(b.getWidth(), 110), juce::jmin(b.getHeight(), 22)));
}

void GlobalPanel::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    fillPanel(g, getLocalBounds().toFloat(), p);
    g.setColour(p.ink3);
    g.setFont(fonts::sans(10.0f, fonts::Weight::medium, 0.08f));
    g.drawText("MASTER", 12, 8, getWidth() - 24, 16, juce::Justification::centredLeft, false);

    g.setFont(fonts::sans(10.0f, fonts::Weight::medium, 0.08f));
    if (deltaShown_)
    {
        g.setColour(p.act);
        g.drawText("DELTA", deltaCaption_, juce::Justification::centred, false);
    }
    g.setColour(p.ink3);
    g.drawText("MONITOR", monitorHead_, juce::Justification::centredLeft, false);
    if (matchText_.isNotEmpty())
    {
        g.setColour(p.ink2);
        g.setFont(fonts::mono(10.0f, fonts::Weight::regular));
        g.drawText(matchText_, monitorHead_, juce::Justification::centredRight, false);
    }

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
