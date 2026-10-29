// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Footer.h"

#include "PluginInfo.h"

namespace pentagrit::ui
{
const char* Footer::defaultHint()
{
    return "Drag a knob. Shift for fine, double-click to reset or type a value (see Settings).";
}

void Footer::setHint(const juce::String& text)
{
    const auto& t = text.isEmpty() ? juce::String(defaultHint()) : text;
    if (t != hint_)
    {
        hint_ = t;
        repaint();
    }
}

void Footer::setCpu(float fraction)
{
    const auto t = "CPU " + juce::String(juce::jlimit(0.0f, 9.99f, fraction) * 100.0f, 1) + "%";
    if (t != cpu_)
    {
        cpu_ = t;
        if (showCpu_)
            repaint();
    }
}

void Footer::setShowCpu(bool show)
{
    if (show != showCpu_)
    {
        showCpu_ = show;
        repaint();
    }
}

void Footer::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    auto b = getLocalBounds().reduced(12, 0);
    g.setFont(fonts::mono(10.5f, fonts::Weight::regular));
    g.setColour(p.ink2);

    if (showCpu_)
        g.drawText(cpu_, b.removeFromRight(86), juce::Justification::centredRight, false);
    g.setColour(p.ink3);
    g.drawText(juce::String("v") + info::kVersion + "  |  AGPL-3.0-or-later", b.removeFromRight(190),
               juce::Justification::centredRight, false);

    g.setColour(p.ink2);
    g.setFont(fonts::sans(11.0f, fonts::Weight::regular));
    g.drawText(hint_, b, juce::Justification::centredLeft, true);
}
} // namespace pentagrit::ui
