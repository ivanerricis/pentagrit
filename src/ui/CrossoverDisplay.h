// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/Meters.h"
#include "ui/Widgets.h"

namespace pentagrit::ui
{
// Log 20 Hz..20 kHz display with the active band regions, per-band peak bars, transient-gain readouts and
// draggable crossover handles (bound to xover1..4 with ParameterAttachment, one gesture per drag).
class CrossoverDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    CrossoverDisplay(juce::AudioProcessorValueTreeState& apvts, MeterState& meters);

    void applyTheme() { repaint(); }
    void setNumBands(int n);
    void tick(); // repaints when the meters changed

    // Geometry helpers (public for tests).
    float freqToX(float hz) const;
    float xToFreq(float x) const;
    juce::Rectangle<float> plotArea() const;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    // Sets handle h (0..3) to hz (clamped to the legal spacing) as one complete gesture; resetHandle uses the
    // default.
    void commitHz(int h, float hz);
    void resetHandle(int h);

private:
    void beginTyping(int h);
    void endTyping(bool commit);
    int handleAt(float x) const;
    float shownHz(int i) const;

    juce::AudioProcessorValueTreeState& apvts_;
    MeterState& meters_;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 4> attachments_;
    std::array<float, 4> paramHz_{120.0f, 1200.0f, 3500.0f, 8000.0f};
    std::array<float, 4> effectiveHz_{120.0f, 1200.0f, 3500.0f, 8000.0f};
    std::array<float, kMaxBands> peak_{}, tg_{};
    int numBands_ = 3;
    int hover_ = -1, dragging_ = -1;
    int typingHandle_ = -1;
    std::unique_ptr<juce::TextEditor> editor_;
};
} // namespace pentagrit::ui
