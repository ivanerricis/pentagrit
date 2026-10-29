// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include <cmath>

namespace pentagrit
{
namespace
{
using namespace ui;

constexpr int kMargin = 8;
constexpr int kTopBarH = 52;
constexpr int kCrossoverY = 60, kCrossoverH = 168;
constexpr int kFooterH = 26;
constexpr int kMasterW = 190;
constexpr int kGap = 6;
constexpr float kMinScale = 0.5f, kMaxScale = 2.0f;

void setParamWithGesture(juce::RangedAudioParameter* p, float denormalised)
{
    if (p == nullptr)
        return;
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(denormalised));
    p->endChangeGesture();
}
} // namespace

PentagritAudioProcessorEditor::PentagritAudioProcessorEditor(PentagritAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor),
      topBar_(processor.getAPVTS(), processor.getMeters(),
              [&processor] { return processor.getSampleRate(); }),
      crossover_(processor.getAPVTS(), processor.getMeters()),
      global_(processor.getAPVTS(), processor.getMeters())
{
    auto& apvts = processor_.getAPVTS();
    setTitle("Pentagrit");
    setLookAndFeel(&lnf_);

    addAndMakeVisible(content_);
    content_.setTitle("Pentagrit");
    content_.addAndMakeVisible(topBar_);
    content_.addAndMakeVisible(crossover_);
    for (int b = 0; b < kMaxBands; ++b)
    {
        strips_[static_cast<size_t>(b)] = std::make_unique<BandStrip>(apvts, processor_.getMeters(), b);
        content_.addAndMakeVisible(*strips_[static_cast<size_t>(b)]);
    }
    content_.addAndMakeVisible(global_);
    content_.addAndMakeVisible(footer_);
    footer_.setInterceptsMouseClicks(false, false);
    content_.onResized = [this] { layoutContent(); };

    numBands_ =
        juce::jlimit(2, kMaxBands, juce::roundToInt(apvts.getRawParameterValue(pid::numBands)->load()));
    numBandsAttachment_ = std::make_unique<juce::ParameterAttachment>(
        *apvts.getParameter(pid::numBands), [this](float v) { applyNumBands(juce::roundToInt(v), true); },
        nullptr);

    // Size: persisted (APVTS uiWidth/uiHeight) when it has our aspect ratio, base size otherwise.
    const auto stored = processor_.getUiSize();
    int w = kBaseWidth, h = kFullHeight;
    const float aspect = static_cast<float>(kBaseWidth) / static_cast<float>(kFullHeight);
    if (std::abs(static_cast<float>(stored.x) / static_cast<float>(stored.y) - aspect) < 0.02f &&
        stored.x >= kBaseWidth * kMinScale && stored.x <= kBaseWidth * kMaxScale)
    {
        w = stored.x;
        h = juce::roundToInt(static_cast<float>(w) / aspect);
    }
    updateConstraints();
    setResizable(true, true);
    setConstrainer(&constrainer_);

    addMouseListener(this, true);
    setUiOptions(options_);
    setSize(w, h);
    lastTickMs_ = juce::Time::getMillisecondCounterHiRes();
    refreshNow();
}

PentagritAudioProcessorEditor::~PentagritAudioProcessorEditor()
{
    stopTimer();
    removeMouseListener(this);
    tooltip_.reset();
    setLookAndFeel(nullptr);
}

void PentagritAudioProcessorEditor::updateConstraints()
{
    const double aspect = static_cast<double>(kBaseWidth) / static_cast<double>(baseHeight());
    constrainer_.setFixedAspectRatio(aspect);
    constrainer_.setSizeLimits(
        juce::roundToInt(kBaseWidth * kMinScale), juce::roundToInt(baseHeight() * kMinScale),
        juce::roundToInt(kBaseWidth * kMaxScale), juce::roundToInt(baseHeight() * kMaxScale));
}

void PentagritAudioProcessorEditor::setUiOptions(const UiOptions& o)
{
    const bool layoutChanged = o.layout != options_.layout;
    const bool themeChanged = o.theme != lnf_.theme();
    options_ = o;
    options_.refreshHz = juce::jlimit(10, 120, o.refreshHz);
    options_.zoomPercent = juce::jlimit(50, 400, o.zoomPercent);

    if (themeChanged)
    {
        lnf_.setTheme(o.theme);
        if (tooltip_ != nullptr)
            tooltip_->sendLookAndFeelChange();
        sendLookAndFeelChange();
    }
    applyThemeToChildren();

    for (auto& s : strips_)
        s->applyOptions(options_);
    global_.applyOptions(options_);
    footer_.setShowCpu(options_.showCpu);

    if (options_.tooltips && tooltip_ == nullptr)
    {
        tooltip_ = std::make_unique<juce::TooltipWindow>(this, 700);
        tooltip_->setLookAndFeel(&lnf_);
    }
    else if (!options_.tooltips && tooltip_ != nullptr)
    {
        tooltip_.reset();
    }

    startTimerHz(options_.refreshHz);
    setScaleFactor(static_cast<float>(options_.zoomPercent) / 100.0f);

    if (layoutChanged)
    {
        updateConstraints();
        // Keep the current width, change the height to the new aspect ratio.
        const int width = juce::jmax(1, getWidth() > 0 ? getWidth() : kBaseWidth);
        setSize(width,
                juce::roundToInt(static_cast<float>(width) * static_cast<float>(baseHeight()) / kBaseWidth));
    }
    content_.setSize(kBaseWidth, baseHeight());
    resized();
    layoutContent();
    repaint();
}

void PentagritAudioProcessorEditor::applyThemeToChildren()
{
    topBar_.applyTheme();
    crossover_.applyTheme();
    for (auto& s : strips_)
        s->applyTheme();
    global_.applyTheme();
}

void PentagritAudioProcessorEditor::showToast(const juce::String& text, int ms)
{
    footer_.setHint(text);
    toastUntilMs_ = juce::Time::getMillisecondCounterHiRes() + ms;
}

void PentagritAudioProcessorEditor::mouseEnter(const juce::MouseEvent& e)
{
    if (juce::Time::getMillisecondCounterHiRes() < toastUntilMs_)
        return;
    juce::String hint;
    for (auto* c = e.eventComponent; c != nullptr && c != this; c = c->getParentComponent())
    {
        if (auto* tc = dynamic_cast<juce::TooltipClient*>(c))
        {
            hint = tc->getTooltip();
            if (hint.isNotEmpty())
                break;
        }
    }
    footer_.setHint(hint);
}

void PentagritAudioProcessorEditor::mouseExit(const juce::MouseEvent& e)
{
    if (e.eventComponent == this && juce::Time::getMillisecondCounterHiRes() >= toastUntilMs_)
        footer_.setHint({});
}

void PentagritAudioProcessorEditor::applyNumBands(int n, bool)
{
    n = juce::jlimit(2, kMaxBands, n);
    const bool raised = n > numBands_;
    numBands_ = n;

    if (raised)
    {
        // Push the handles up so that they keep the 1/3 octave spacing.
        auto& apvts = processor_.getAPVTS();
        float prev = apvts.getRawParameterValue(pid::xover[0])->load();
        for (size_t i = 1; i < pid::xover.size(); ++i)
        {
            const float cur = apvts.getRawParameterValue(pid::xover[i])->load();
            const float minimum = prev * xover::ratio;
            if (cur < minimum)
            {
                const float fixed = juce::jmin(xover::maxFreq / xover::ratio, minimum);
                setParamWithGesture(apvts.getParameter(pid::xover[i]), fixed);
                prev = fixed;
            }
            else
            {
                prev = cur;
            }
        }
    }
    crossover_.setNumBands(n);
    for (auto& s : strips_)
        s->setNumBands(n);
    layoutContent();
}

void PentagritAudioProcessorEditor::layoutContent()
{
    const bool compact = isCompact();
    const int w = kBaseWidth, h = baseHeight();
    topBar_.setBounds(kMargin, 4, w - 2 * kMargin, kTopBarH);
    crossover_.setVisible(!compact);
    crossover_.setBounds(kMargin, kCrossoverY, w - 2 * kMargin, kCrossoverH);
    footer_.setBounds(0, h - kFooterH, w, kFooterH);

    const int bodyTop = compact ? kCrossoverY : kCrossoverY + kCrossoverH + kMargin;
    const int bodyBottom = h - kFooterH - kMargin;
    juce::Rectangle<int> body(kMargin, bodyTop, w - 2 * kMargin, bodyBottom - bodyTop);

    global_.setBounds(body.removeFromRight(kMasterW));
    body.removeFromRight(kGap);
    const int n = numBands_;
    const int stripW = (body.getWidth() - (n - 1) * kGap) / n;
    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& s = *strips_[static_cast<size_t>(b)];
        s.setVisible(b < n);
        if (b < n)
        {
            const int x = body.getX() + b * (stripW + kGap);
            s.setBounds(x, body.getY(), b == n - 1 ? body.getRight() - x : stripW, body.getHeight());
        }
    }
}

void PentagritAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / static_cast<float>(kBaseWidth);
    content_.setBounds(0, 0, kBaseWidth, baseHeight());
    content_.setTransform(juce::AffineTransform::scale(s));
    processor_.setUiSize(getWidth(), getHeight());
}

void PentagritAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(lnf_.pal().bg0);
}

void PentagritAudioProcessorEditor::timerCallback()
{
    refreshNow();
}

void PentagritAudioProcessorEditor::refreshNow()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = juce::jlimit(0.001f, 0.25f, static_cast<float>((now - lastTickMs_) * 0.001));
    lastTickMs_ = now;

    auto& apvts = processor_.getAPVTS();
    const int n =
        juce::jlimit(2, kMaxBands, juce::roundToInt(apvts.getRawParameterValue(pid::numBands)->load()));
    if (n != numBands_)
        applyNumBands(n, false);

    bool anySolo = false;
    for (int b = 0; b < numBands_; ++b)
        anySolo = anySolo || strips_[static_cast<size_t>(b)]->isSolo();
    for (int b = 0; b < numBands_; ++b)
    {
        auto& s = *strips_[static_cast<size_t>(b)];
        s.setDimmed(s.isMuted() || (anySolo && !s.isSolo()));
        s.tick(dt);
    }
    topBar_.tick();
    if (!isCompact())
        crossover_.tick();
    global_.tick(dt);
    footer_.setCpu(processor_.getMeters().cpuLoad.load(std::memory_order_relaxed));
}
} // namespace pentagrit
