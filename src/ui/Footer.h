// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Widgets.h"

namespace pentagrit::ui
{
// Bottom line: hover hint (left), version, licence and CPU readout (right).
class Footer final : public juce::Component
{
public:
    static const char* defaultHint();

    void setHint(const juce::String& text);
    void setCpu(float fraction);
    void setShowCpu(bool show);
    const juce::String& hint() const noexcept { return hint_; }
    const juce::String& cpuText() const noexcept { return cpu_; }

    void paint(juce::Graphics&) override;

private:
    juce::String hint_{defaultHint()}, cpu_{"CPU 0.0%"};
    bool showCpu_ = true;
};
} // namespace pentagrit::ui
