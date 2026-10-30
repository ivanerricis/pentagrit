// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Widgets.h"

#include "dsp/Saturator.h"
#include <cmath>

namespace pentagrit::ui
{
namespace
{
constexpr float kRotaryStart = juce::MathConstants<float>::pi * 1.25f;
constexpr float kRotaryEnd = juce::MathConstants<float>::pi * 2.75f;

float dbToUnit(float db)
{
    return juce::jlimit(0.0f, 1.0f, (db - LevelMeter::floorDb) / -LevelMeter::floorDb);
}
} // namespace

// ============================== KnobSlider
// ====================================================================
KnobSlider::KnobSlider()
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters(kRotaryStart, kRotaryEnd, true);
    setSliderSnapsToMousePosition(false);
    setMouseDragSensitivity(170);
    setPopupMenuEnabled(false);
}

void KnobSlider::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (typeOnDoubleClick && onTypeRequest)
        onTypeRequest();
    else
        juce::Slider::mouseDoubleClick(e);
}

void KnobSlider::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (!isEnabled() || w.deltaY == 0.0f)
        return;
    const double dir = (w.deltaY > 0.0f ? 1.0 : -1.0) * (w.isReversed ? -1.0 : 1.0);
    const double p = juce::jlimit(0.0, 1.0, valueToProportionOfLength(getValue()) + dir * 0.02);
    setValue(proportionOfLengthToValue(p), juce::sendNotificationSync);
}

// ============================== LabelledKnob
// ==================================================================
LabelledKnob::LabelledKnob(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                           const juce::String& label, const juce::String& hint, Fmt fmt, bool bipolar,
                           int knobSize)
    : label_(label.toUpperCase()), fmt_(fmt), knobSize_(knobSize)
{
    auto* param = apvts.getParameter(paramID);
    jassert(param != nullptr);

    setTitle(label);
    setDescription(hint);
    setTooltip(hint);

    slider_.setTitle(label);
    slider_.setDescription(hint);
    slider_.setTooltip(hint);
    slider_.getProperties().set(prop::bipolar, bipolar);
    addAndMakeVisible(slider_);

    attachment_ = std::make_unique<juce::SliderParameterAttachment>(*param, slider_, nullptr);
    slider_.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
    slider_.onValueChange = [this] { updateText(); };
    slider_.onTypeRequest = [this] { beginTyping(); };
    updateText();
}

void LabelledKnob::setColour(juce::Colour c)
{
    slider_.setColour(juce::Slider::rotarySliderFillColourId, c);
    slider_.repaint();
}

void LabelledKnob::applyOptions(const UiOptions& o)
{
    const bool circular = o.knob == KnobMode::circular;
    slider_.setSliderStyle(circular ? juce::Slider::Rotary : juce::Slider::RotaryVerticalDrag);
    slider_.setRotaryParameters(kRotaryStart, kRotaryEnd, true);
    slider_.setMouseDragSensitivity(juce::jmax(
        20,
        juce::roundToInt(170.0f / std::pow(4.0f, (juce::jlimit(0.0f, 1.0f, o.sensitivity) - 0.5f) * 2.0f))));
    slider_.typeOnDoubleClick = o.dc == DoubleClick::type;
    if (o.dc != DoubleClick::type && editor_ != nullptr)
        endTyping(false);
}

void LabelledKnob::updateText()
{
    const double v = slider_.getValue();
    juce::String t;
    switch (fmt_)
    {
        case Fmt::dbSigned:
            t = (v > 0.049 ? "+" : "") + juce::String(v, 1);
            break;
        case Fmt::dbSignedUnit:
            t = (v > 0.049 ? "+" : "") + juce::String(v, 1) + " dB";
            break;
        case Fmt::dbUnit:
            t = juce::String(v, 1) + " dB";
            break;
        case Fmt::percent:
            t = juce::String(juce::roundToInt(v * 100.0)) + "%";
            break;
        case Fmt::speed:
            t = "x" + juce::String(std::pow(4.0, (v - 0.5) * 2.0), 2);
            break;
    }
    if (t != valueText_)
    {
        valueText_ = t;
        repaint();
    }
}

double LabelledKnob::parse(const juce::String& text) const
{
    const double n = text.retainCharacters("0123456789.-+").getDoubleValue();
    switch (fmt_)
    {
        case Fmt::percent:
            return n / 100.0;
        case Fmt::speed:
            return n > 0.0 ? 0.5 + 0.5 * std::log(n) / std::log(4.0) : 0.0;
        default:
            return n;
    }
}

void LabelledKnob::beginTyping()
{
    if (editor_ != nullptr)
        return;
    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setTitle(label_);
    editor_->setJustification(juce::Justification::centred);
    editor_->setFont(fonts::mono(10.5f, fonts::Weight::regular));
    editor_->setIndents(0, 0);
    editor_->setBorder({0, 0, 0, 0});
    editor_->setText(valueText_.retainCharacters("0123456789.-+x%"), false);
    editor_->onReturnKey = [this] { endTyping(true); };
    editor_->onEscapeKey = [this] { endTyping(false); };
    editor_->onFocusLost = [this] { endTyping(true); };
    addAndMakeVisible(*editor_);
    editor_->setBounds(getLocalBounds().removeFromBottom(14).reduced(2, 0));
    editor_->grabKeyboardFocus();
    editor_->selectAll();
}

void LabelledKnob::endTyping(bool commit)
{
    if (editor_ == nullptr)
        return;
    auto ed = std::move(editor_);
    ed->onFocusLost = nullptr;
    if (commit)
    {
        const auto v = parse(ed->getText());
        slider_.setValue(juce::jlimit(slider_.getMinimum(), slider_.getMaximum(), v),
                         juce::sendNotificationSync);
    }
    removeChildComponent(ed.get());
    // Destroy asynchronously: this can be called from the editor's own callback.
    juce::MessageManager::callAsync([keep = std::shared_ptr<juce::TextEditor>(std::move(ed))] {});
    repaint();
}

void LabelledKnob::resized()
{
    auto b = getLocalBounds();
    b.removeFromTop(12);
    auto kb = b.removeFromTop(knobSize_ + 2);
    slider_.setBounds(kb.withSizeKeepingCentre(knobSize_, knobSize_));
    if (editor_ != nullptr)
        editor_->setBounds(getLocalBounds().removeFromBottom(14).reduced(2, 0));
}

void LabelledKnob::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    auto b = getLocalBounds();
    g.setColour(p.ink3);
    g.setFont(fonts::sans(10.0f, fonts::Weight::medium, 0.08f));
    g.drawFittedText(label_, b.removeFromTop(12), juce::Justification::centred, 1, 0.7f);
    if (editor_ == nullptr)
    {
        g.setColour(p.ink2);
        g.setFont(fonts::mono(10.0f, fonts::Weight::regular));
        g.drawText(valueText_, getLocalBounds().removeFromBottom(14), juce::Justification::centred, false);
    }
}

// ============================== LevelMeter
// ====================================================================
void LevelMeter::update(float peakDb, float dt)
{
    const float prevLevel = levelDb_, prevHold = holdDb_;
    peakDb = juce::jmax(peakDb, -100.0f);
    levelDb_ = peakDb >= levelDb_ ? peakDb : juce::jmax(peakDb, levelDb_ - releaseDbPerSec_ * dt);

    if (holdSec_ == 0.0f)
    {
        holdDb_ = -100.0f;
    }
    else if (peakDb >= holdDb_)
    {
        holdDb_ = peakDb;
        holdAge_ = 0.0f;
    }
    else
    {
        holdAge_ += dt;
        if (holdSec_ > 0.0f && holdAge_ > holdSec_)
            holdDb_ = juce::jmax(levelDb_, holdDb_ - releaseDbPerSec_ * dt);
    }
    if (std::abs(levelDb_ - prevLevel) > 0.05f || std::abs(holdDb_ - prevHold) > 0.05f)
        repaint();
}

void LevelMeter::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    const auto b = getLocalBounds().toFloat();
    g.setColour(p.bg0.getBrightness() > 0.5f ? p.bg3 : p.bg2);
    g.fillRoundedRectangle(b, 2.0f);

    const float u = dbToUnit(levelDb_);
    if (u > 0.0f)
    {
        auto bar = b.reduced(1.0f);
        const float h = bar.getHeight() * u;
        g.setColour(levelDb_ > -3.0f ? p.warn : colour_);
        g.fillRect(bar.withTop(bar.getBottom() - h));
    }
    if (holdDb_ > floorDb)
    {
        const auto inner = b.reduced(1.0f);
        const float y = inner.getBottom() - inner.getHeight() * dbToUnit(holdDb_);
        g.setColour(holdDb_ > -3.0f ? p.warn : p.ink);
        g.fillRect(inner.getX(), juce::jlimit(inner.getY(), inner.getBottom() - 1.0f, y), inner.getWidth(),
                   1.5f);
    }
}

// ============================== SegmentedSelector
// ============================================================
SegmentedSelector::SegmentedSelector(juce::RangedAudioParameter& param, const juce::StringArray& labels,
                                     int firstValue, const juce::String& title, const juce::String& hint)
    : labels_(labels), first_(firstValue)
{
    setTitle(title);
    setDescription(hint);
    setTooltip(hint);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    attachment_ = std::make_unique<juce::ParameterAttachment>(
        param,
        [this](float v)
        {
            const int idx = juce::jlimit(0, labels_.size() - 1, juce::roundToInt(v) - first_);
            if (idx != selected_)
            {
                selected_ = idx;
                repaint();
            }
        },
        nullptr);
    attachment_->sendInitialUpdate();
}

void SegmentedSelector::select(int index)
{
    index = juce::jlimit(0, labels_.size() - 1, index);
    selected_ = index;
    attachment_->setValueAsCompleteGesture(static_cast<float>(first_ + index));
    repaint();
}

int SegmentedSelector::indexAt(juce::Point<int> pt) const
{
    const auto inner = getLocalBounds().reduced(2);
    return juce::jlimit(0, labels_.size() - 1,
                        (pt.x - inner.getX()) * labels_.size() / juce::jmax(1, inner.getWidth()));
}

void SegmentedSelector::mouseDown(const juce::MouseEvent& e)
{
    const int i = indexAt(e.getPosition());
    if (i != selected_)
        select(i);
}

bool SegmentedSelector::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::leftKey || k == juce::KeyPress::downKey)
    {
        select(selected_ - 1);
        return true;
    }
    if (k == juce::KeyPress::rightKey || k == juce::KeyPress::upKey)
    {
        select(selected_ + 1);
        return true;
    }
    return false;
}

void SegmentedSelector::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    g.setColour(p.bg0);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), p.rButton);
    const auto inner = getLocalBounds().toFloat().reduced(2.0f);
    const float w = inner.getWidth() / static_cast<float>(labels_.size());
    for (int i = 0; i < labels_.size(); ++i)
    {
        const auto r = juce::Rectangle<float>(inner.getX() + w * static_cast<float>(i), inner.getY(), w,
                                              inner.getHeight());
        const bool on = i == selected_;
        if (on)
        {
            g.setColour(p.act);
            g.fillRoundedRectangle(r.reduced(0.5f, 0.0f), p.rSmall);
        }
        g.setColour(on ? p.onAct : p.ink2);
        g.setFont(fonts::sans(11.5f, on ? fonts::Weight::semiBold : fonts::Weight::medium));
        g.drawText(labels_[i], r.toNearestInt(), juce::Justification::centred, false);
    }
    if (hasKeyboardFocus(true))
    {
        g.setColour(p.ink2);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
    }
}

// ============================== SatTypeSelector
// ==============================================================
SatTypeSelector::SatTypeSelector(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                                 const juce::String& title)
{
    param_ = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramID));
    jassert(param_ != nullptr);
    setTitle(title);
    prev_.setTitle(title + " previous");
    next_.setTitle(title + " next");
    name_.setTitle(title);
    for (auto* b : {&prev_, &name_, &next_})
    {
        b->setTooltip(b == &name_ ? "Saturation curve (click to step)"
                                  : (b == &prev_ ? "Previous curve" : "Next curve"));
        addAndMakeVisible(*b);
    }
    prev_.onClick = [this] { step(-1); };
    next_.onClick = [this] { step(1); };
    name_.onClick = [this] { step(1); };
    attachment_ = std::make_unique<juce::ParameterAttachment>(
        *param_,
        [this](float v)
        {
            type_ = juce::jlimit(0, param_->choices.size() - 1, juce::roundToInt(v));
            name_.setButtonText(param_->choices[type_]);
            if (onTypeChanged)
                onTypeChanged(type_);
        },
        nullptr);
    attachment_->sendInitialUpdate();
}

void SatTypeSelector::step(int d)
{
    const int n = param_->choices.size();
    attachment_->setValueAsCompleteGesture(static_cast<float>((type_ + d + n) % n));
}

void SatTypeSelector::resized()
{
    auto b = getLocalBounds();
    prev_.setBounds(b.removeFromLeft(22));
    next_.setBounds(b.removeFromRight(22));
    name_.setBounds(b.reduced(2, 0));
}

// ============================== CurvePreview
// =================================================================
void CurvePreview::setType(int t)
{
    if (t != type_)
    {
        type_ = t;
        rebuild();
        repaint();
    }
}

void CurvePreview::setDriveDb(float db)
{
    if (std::abs(db - driveDb_) > 0.01f)
    {
        driveDb_ = db;
        rebuild();
        repaint();
    }
}

void CurvePreview::rebuild()
{
    curve_.clear();
    const auto b = getLocalBounds().toFloat().reduced(3.0f);
    if (b.isEmpty())
        return;
    const float gain = 1.0f + driveDb_ / 36.0f * 6.0f;
    constexpr int n = 72;
    constexpr float clipY = 1.15f;
    for (int i = 0; i <= n; ++i)
    {
        const float x = -1.0f + 2.0f * static_cast<float>(i) / n;
        float y = x;
        if (type_ != 0)
            y = Saturator::shape(static_cast<SatType>(type_), x * gain);
        y = juce::jlimit(-clipY, clipY, y);
        const juce::Point<float> pt(b.getX() + (x + 1.0f) * 0.5f * b.getWidth(),
                                    b.getCentreY() - y / clipY * 0.5f * b.getHeight());
        if (i == 0)
            curve_.startNewSubPath(pt);
        else
            curve_.lineTo(pt);
    }
}

void CurvePreview::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    const auto b = getLocalBounds().toFloat();
    g.setColour(p.bg2);
    g.fillRoundedRectangle(b, 3.0f);
    const auto in = b.reduced(3.0f);
    g.setColour(p.bg3);
    g.drawHorizontalLine(juce::roundToInt(in.getCentreY()), in.getX(), in.getRight());
    g.drawVerticalLine(juce::roundToInt(in.getCentreX()), in.getY(), in.getBottom());
    g.setColour(p.bg4);
    g.drawLine(in.getX(), in.getBottom(), in.getRight(), in.getY(), 1.0f);
    g.setColour(colour_);
    g.strokePath(curve_,
                 juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ============================== TransientIndicator
// ===========================================================
void TransientIndicator::setGainDb(float db)
{
    db = juce::jlimit(-24.0f, 24.0f, db);
    if (std::abs(db - gainDb_) > 0.04f)
    {
        gainDb_ = db;
        repaint();
    }
}

void TransientIndicator::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    auto b = getLocalBounds().toFloat();
    auto text = b.removeFromRight(46.0f);
    g.setColour(p.ink2);
    g.setFont(fonts::mono(10.0f, fonts::Weight::regular));
    g.drawText((gainDb_ > 0.049f ? "+" : "") + juce::String(gainDb_, 1) + " dB", text.toNearestInt(),
               juce::Justification::centredRight, false);

    auto bar = b.withSizeKeepingCentre(b.getWidth() - 4.0f, 6.0f);
    g.setColour(p.bg2);
    g.fillRoundedRectangle(bar, 3.0f);
    const float cx = bar.getCentreX();
    const float x = cx + gainDb_ / 24.0f * bar.getWidth() * 0.5f;
    g.setColour(colour_);
    g.fillRect(juce::Rectangle<float>(juce::jmin(cx, x), bar.getY(), std::abs(x - cx), bar.getHeight()));
    g.setColour(p.ink3);
    g.fillRect(cx - 0.5f, bar.getY() - 2.0f, 1.0f, bar.getHeight() + 4.0f);
}

// ============================== xover helpers
// ================================================================
namespace xover
{
juce::Range<float> allowedRange(int index, const std::array<float, 4>& hz, int numBands)
{
    float lo = minFreq * ratio, hi = maxFreq / ratio;
    if (index > 0)
        lo = hz[static_cast<size_t>(index - 1)] * ratio;
    if (index + 1 < numBands - 1)
        hi = hz[static_cast<size_t>(index + 1)] / ratio;
    return {lo, juce::jmax(lo, hi)};
}

float clamp(int index, float v, const std::array<float, 4>& hz4, int numBands)
{
    const auto r = allowedRange(index, hz4, numBands);
    return juce::jlimit(r.getStart(), r.getEnd(), v);
}

bool parseHz(const juce::String& text, float& out)
{
    const auto t = text.trim().toLowerCase();
    if (!t.containsAnyOf("0123456789"))
        return false;
    double v = t.retainCharacters("0123456789.").getDoubleValue();
    if (t.contains("k"))
        v *= 1000.0;
    out = static_cast<float>(v);
    return true;
}

juce::String format(float hz)
{
    return hz >= 1000.0f ? juce::String(hz / 1000.0f, 2) + " kHz"
                         : juce::String(juce::roundToInt(hz)) + " Hz";
}

juce::String formatShort(float hz)
{
    if (hz >= 1000.0f)
    {
        auto s = juce::String(hz / 1000.0f, 2);
        while (s.endsWithChar('0'))
            s = s.dropLastCharacters(1);
        if (s.endsWithChar('.'))
            s = s.dropLastCharacters(1);
        return s + "k";
    }
    return juce::String(juce::roundToInt(hz));
}
} // namespace xover
} // namespace pentagrit::ui
