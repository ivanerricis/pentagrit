// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/CrossoverDisplay.h"

#include <cmath>

namespace pentagrit::ui
{
namespace
{
constexpr float kPadTop = 26.0f, kPadBottom = 18.0f, kPadSide = 18.0f;
constexpr float kHitZone = 14.0f;
constexpr float kSilentDb = -60.0f;
} // namespace

CrossoverDisplay::CrossoverDisplay(juce::AudioProcessorValueTreeState& apvts, MeterState& meters)
    : apvts_(apvts), meters_(meters)
{
    setTitle("Crossover display");
    setTooltip(
        "Crossover: drag the handles (log 20 Hz to 20 kHz, 1/3 octave minimum spacing), double-click resets");
    for (size_t i = 0; i < 4; ++i)
    {
        auto* param = apvts.getParameter(pid::xover[i]);
        attachments_[i] = std::make_unique<juce::ParameterAttachment>(
            *param,
            [this, i](float v)
            {
                paramHz_[i] = v;
                repaint();
            },
            nullptr);
        attachments_[i]->sendInitialUpdate();
    }
    effectiveHz_ = paramHz_;
}

void CrossoverDisplay::setNumBands(int n)
{
    n = juce::jlimit(2, kMaxBands, n);
    if (n != numBands_)
    {
        numBands_ = n;
        repaint();
    }
}

juce::Rectangle<float> CrossoverDisplay::plotArea() const
{
    return {kPadSide, kPadTop, static_cast<float>(getWidth()) - 2.0f * kPadSide,
            static_cast<float>(getHeight()) - kPadTop - kPadBottom};
}

float CrossoverDisplay::freqToX(float hz) const
{
    const auto a = plotArea();
    const float t = std::log(juce::jlimit(xover::minFreq, xover::maxFreq, hz) / xover::minFreq) /
                    std::log(xover::maxFreq / xover::minFreq);
    return a.getX() + t * a.getWidth();
}

float CrossoverDisplay::xToFreq(float x) const
{
    const auto a = plotArea();
    const float t = juce::jlimit(0.0f, 1.0f, (x - a.getX()) / a.getWidth());
    return xover::minFreq * std::pow(xover::maxFreq / xover::minFreq, t);
}

float CrossoverDisplay::shownHz(int i) const
{
    return dragging_ == i ? paramHz_[static_cast<size_t>(i)] : effectiveHz_[static_cast<size_t>(i)];
}

void CrossoverDisplay::tick()
{
    bool changed = false;
    for (size_t i = 0; i < 4; ++i)
    {
        const float v = meters_.effectiveXoverHz[i].load(std::memory_order_relaxed);
        if (std::abs(v - effectiveHz_[i]) > 0.01f)
        {
            effectiveHz_[i] = v;
            changed = true;
        }
    }
    for (size_t b = 0; b < kMaxBands; ++b)
    {
        const float p = meters_.bandPeakDb[b].load(std::memory_order_relaxed);
        const float t = meters_.bandTransientGainDb[b].load(std::memory_order_relaxed);
        if (std::abs(p - peak_[b]) > 0.2f || std::abs(t - tg_[b]) > 0.04f)
        {
            peak_[b] = p;
            tg_[b] = t;
            changed = true;
        }
    }
    if (changed)
        repaint();
}

int CrossoverDisplay::handleAt(float x) const
{
    int best = -1;
    float bestD = kHitZone;
    for (int i = 0; i < numBands_ - 1; ++i)
    {
        const float d = std::abs(freqToX(shownHz(i)) - x);
        if (d <= bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void CrossoverDisplay::mouseMove(const juce::MouseEvent& e)
{
    const int h = handleAt(e.position.x);
    setMouseCursor(h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    if (h != hover_)
    {
        hover_ = h;
        repaint();
    }
}

void CrossoverDisplay::mouseExit(const juce::MouseEvent&)
{
    if (hover_ != -1)
    {
        hover_ = -1;
        repaint();
    }
}

void CrossoverDisplay::mouseDown(const juce::MouseEvent& e)
{
    dragging_ = handleAt(e.position.x);
    if (dragging_ >= 0)
    {
        paramHz_ = effectiveHz_; // start from what the user sees
        attachments_[static_cast<size_t>(dragging_)]->beginGesture();
        repaint();
    }
}

void CrossoverDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ < 0)
        return;
    std::array<float, 4> cur{};
    for (size_t i = 0; i < 4; ++i)
        cur[i] = apvts_.getRawParameterValue(pid::xover[i])->load();
    const float hz = xover::clamp(dragging_, xToFreq(e.position.x), cur, numBands_);
    attachments_[static_cast<size_t>(dragging_)]->setValueAsPartOfGesture(hz);
}

void CrossoverDisplay::mouseUp(const juce::MouseEvent&)
{
    if (dragging_ >= 0)
    {
        attachments_[static_cast<size_t>(dragging_)]->endGesture();
        dragging_ = -1;
        repaint();
    }
}

void CrossoverDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int h = handleAt(e.position.x);
    if (h < 0)
        return;
    auto* param = apvts_.getParameter(pid::xover[static_cast<size_t>(h)]);
    std::array<float, 4> cur{};
    for (size_t i = 0; i < 4; ++i)
        cur[i] = apvts_.getRawParameterValue(pid::xover[i])->load();
    const float def = xover::clamp(h, param->convertFrom0to1(param->getDefaultValue()), cur, numBands_);
    attachments_[static_cast<size_t>(h)]->setValueAsCompleteGesture(def);
}

void CrossoverDisplay::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    const auto area = getLocalBounds().toFloat();
    g.setColour(p.bg1);
    g.fillRoundedRectangle(area, 6.0f);

    const auto plot = plotArea();
    g.setColour(p.bg0);
    g.fillRoundedRectangle(plot, 3.0f);

    // Grid: vertical lines at 1-2-5 steps, horizontal quarters.
    static constexpr float gridHz[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    static constexpr const char* gridTxt[] = {"20", "50", "100", "200", "500",
                                              "1k", "2k", "5k",  "10k", "20k"};
    g.setFont(fonts::mono(10.0f, fonts::Weight::regular));
    for (int i = 0; i < 10; ++i)
    {
        const float x = freqToX(gridHz[i]);
        g.setColour(p.bg3.withAlpha(i == 0 || i == 9 ? 0.0f : 0.8f));
        g.fillRect(x - 0.5f, plot.getY(), 1.0f, plot.getHeight());
        g.setColour(p.ink3);
        g.drawText(gridTxt[i],
                   juce::Rectangle<float>(x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f).toNearestInt(),
                   juce::Justification::centred, false);
    }
    for (int i = 1; i < 4; ++i)
    {
        g.setColour(p.bg3.withAlpha(0.5f));
        g.fillRect(plot.getX(), plot.getY() + plot.getHeight() * static_cast<float>(i) / 4.0f,
                   plot.getWidth(), 1.0f);
    }

    // Band regions.
    float edges[kMaxBands + 1];
    edges[0] = xover::minFreq;
    for (int i = 0; i < numBands_ - 1; ++i)
        edges[i + 1] = shownHz(i);
    edges[numBands_] = xover::maxFreq;

    for (int b = 0; b < numBands_; ++b)
    {
        const auto col = p.band[static_cast<size_t>(b)];
        const float x0 = freqToX(edges[b]), x1 = freqToX(edges[b + 1]);
        const bool silent = peak_[static_cast<size_t>(b)] <= kSilentDb;
        g.setColour(col.withAlpha(silent ? 0.10f : 0.18f));
        g.fillRect(juce::Rectangle<float>(x0, plot.getY(), x1 - x0, plot.getHeight()));

        // Peak bar rising from the bottom.
        const float u = juce::jlimit(0.0f, 1.0f, (peak_[static_cast<size_t>(b)] - kSilentDb) / -kSilentDb);
        if (x1 - x0 > 8.0f)
        {
            const float h = plot.getHeight() * u;
            g.setColour(col.withAlpha(silent ? 0.25f : 0.70f));
            g.fillRect(juce::Rectangle<float>(x0 + 3.0f, plot.getBottom() - h, x1 - x0 - 6.0f, h));
        }

        g.setColour(col);
        g.setFont(fonts::sans(11.0f, fonts::Weight::semiBold));
        g.drawText("B" + juce::String(b + 1),
                   juce::Rectangle<float>(x0 + 6.0f, plot.getY() + 4.0f, 28.0f, 14.0f).toNearestInt(),
                   juce::Justification::centredLeft, false);
        if (x1 - x0 > 110.0f)
        {
            const float tg = tg_[static_cast<size_t>(b)];
            g.setColour(p.ink2);
            g.setFont(fonts::mono(10.0f, fonts::Weight::regular));
            g.drawText((tg > 0.049f ? "+" : "") + juce::String(tg, 1) + " dB",
                       juce::Rectangle<float>(x1 - 66.0f, plot.getY() + 4.0f, 60.0f, 14.0f).toNearestInt(),
                       juce::Justification::centredRight, false);
        }
    }

    // Handles.
    for (int i = 0; i < numBands_ - 1; ++i)
    {
        const float x = freqToX(shownHz(i));
        const bool active = hover_ == i || dragging_ == i;
        g.setColour(p.ink);
        g.fillRect(x - 1.0f, plot.getY(), 2.0f, plot.getHeight());
        const auto txt = xover::format(shownHz(i));
        const float w = 56.0f;
        const auto chip = juce::Rectangle<float>(x - w * 0.5f, 4.0f, w, 18.0f);
        g.setColour(active ? p.ink : p.bg3);
        g.fillRoundedRectangle(chip, 3.0f);
        g.setColour(active ? p.bg0 : p.ink);
        g.setFont(fonts::mono(10.5f, fonts::Weight::medium));
        g.drawText(txt, chip.toNearestInt(), juce::Justification::centred, false);
    }
}
} // namespace pentagrit::ui
