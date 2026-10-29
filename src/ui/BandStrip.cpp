// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/BandStrip.h"

namespace pentagrit::ui
{
namespace
{
using Fmt = LabelledKnob::Fmt;
constexpr int kKnobCellH = 12 + 40 + 14;

void setupToggle(juce::ToggleButton& b, const char* kind, const juce::String& title, const juce::String& hint)
{
    b.getProperties().set(prop::kind, kind);
    b.setTitle(title);
    b.setTooltip(hint);
    b.setWantsKeyboardFocus(true);
}

void setupFreqLabel(juce::Label& l, const juce::String& title)
{
    l.setTitle(title);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(fonts::mono(10.5f, fonts::Weight::regular));
    l.setTooltip("Crossover frequency: type Hz or e.g. 1.2k");
}
} // namespace

void BandStrip::Inner::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    g.setColour(p.bg0);
    for (const auto& r : sections)
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
}

BandStrip::BandStrip(juce::AudioProcessorValueTreeState& apvts, MeterState& meters, int bandIndex)
    : apvts_(apvts), meters_(meters), index_(bandIndex),
      attack_(apvts, bandParamID(bandIndex, BandParam::attack), "Attack",
              "Attack gain in dB (boosts or softens the onset)", Fmt::dbSigned, true, 38),
      sustain_(apvts, bandParamID(bandIndex, BandParam::sustain), "Sustain",
               "Sustain gain in dB (boosts or tightens the tail)", Fmt::dbSigned, true, 38),
      speed_(apvts, bandParamID(bandIndex, BandParam::speed), "Speed",
             "Detector speed, x0.25 (slow) to x4 (fast)", Fmt::speed, false, 38),
      sat_(apvts, bandParamID(bandIndex, BandParam::satType),
           "Band " + juce::String(bandIndex + 1) + " saturation"),
      drive_(apvts, bandParamID(bandIndex, BandParam::drive), "Drive",
             "Drive into the saturation curve (0 to 36 dB)", Fmt::dbUnit, false, 38),
      mix_(apvts, bandParamID(bandIndex, BandParam::mix), "Mix", "Dry/wet of the saturation in this band",
           Fmt::percent, false, 38),
      gain_(apvts, bandParamID(bandIndex, BandParam::gain), "Gain", "Band output gain", Fmt::dbSignedUnit,
            true, 42),
      knobs_{&attack_, &sustain_, &speed_, &drive_, &mix_, &gain_},
      muteRaw_(apvts.getRawParameterValue(bandParamID(bandIndex, BandParam::mute))),
      soloRaw_(apvts.getRawParameterValue(bandParamID(bandIndex, BandParam::solo))),
      driveRaw_(apvts.getRawParameterValue(bandParamID(bandIndex, BandParam::drive)))
{
    const auto bn = "Band " + juce::String(index_ + 1);
    setTitle(bn);
    setupToggle(mute_, "mute", bn + " mute", "Mute this band");
    setupToggle(solo_, "solo", bn + " solo", "Solo this band");
    setupToggle(bypass_, "bypass", bn + " bypass", "Bypass the processing of this band");
    for (auto* b : {&mute_, &solo_, &bypass_})
        addAndMakeVisible(*b);
    auto attach = [this](BandParam bp, juce::ToggleButton& btn)
    {
        return std::make_unique<juce::ButtonParameterAttachment>(
            *apvts_.getParameter(bandParamID(index_, bp)), btn, nullptr);
    };
    muteAtt_ = attach(BandParam::mute, mute_);
    soloAtt_ = attach(BandParam::solo, solo_);
    bypassAtt_ = attach(BandParam::bypass, bypass_);

    setupFreqLabel(from_, bn + " lower crossover");
    setupFreqLabel(to_, bn + " upper crossover");
    for (auto* l : {&fromCaption_, &toCaption_})
    {
        l->setInterceptsMouseClicks(false, false);
        l->setBorderSize({0, 0, 0, 0});
        l->setJustificationType(juce::Justification::centredRight);
        l->setFont(fonts::sans(10.0f, fonts::Weight::medium, 0.08f));
    }
    fromCaption_.setText("FROM", juce::dontSendNotification);
    toCaption_.setText("TO", juce::dontSendNotification);
    from_.onTextChange = [this] { commitFreq(false, from_.getText()); };
    to_.onTextChange = [this] { commitFreq(true, to_.getText()); };
    for (auto* l : {&from_, &to_, &fromCaption_, &toCaption_})
        addChildComponent(*l);

    addAndMakeVisible(inner_);
    inner_.setInterceptsMouseClicks(false, true);
    transient_.setTooltip("Live transient gain applied to this band");
    meter_.setTooltip("Band output peak");
    meter_.setTitle(bn + " output level");
    for (auto* c : std::initializer_list<juce::Component*>{&transient_, &attack_, &sustain_, &speed_, &sat_,
                                                           &curve_, &drive_, &mix_, &gain_, &meter_})
        inner_.addAndMakeVisible(*c);

    sat_.onTypeChanged = [this](int t) { curve_.setType(t); };
    curve_.setType(sat_.type());
    curve_.setTooltip("Saturation transfer curve (preview)");
    curve_.setTitle(bn + " saturation curve");
    curve_.setDriveDb(driveRaw_->load());
    applyTheme();
}

bool BandStrip::isMuted() const noexcept
{
    return muteRaw_->load(std::memory_order_relaxed) >= 0.5f;
}
bool BandStrip::isSolo() const noexcept
{
    return soloRaw_->load(std::memory_order_relaxed) >= 0.5f;
}

std::array<float, 4> BandStrip::xoverValues() const
{
    std::array<float, 4> v{};
    for (size_t i = 0; i < 4; ++i)
        v[i] = apvts_.getRawParameterValue(pid::xover[i])->load();
    return v;
}

void BandStrip::applyTheme()
{
    const auto& p = pal(*this);
    const auto c = p.band[static_cast<size_t>(index_)];
    for (auto* k : knobs_)
        k->setColour(c);
    curve_.setBandColour(c);
    transient_.setBandColour(c);
    meter_.setBarColour(c);
    repaint();
}

void BandStrip::applyOptions(const UiOptions& o)
{
    for (auto* k : knobs_)
        k->applyOptions(o);
    meter_.setBallistics(o.meterHoldSec, o.meterReleaseDbPerSec);
    compact_ = o.layout == Layout::compact;
    for (auto* l : {&from_, &to_, &fromCaption_, &toCaption_})
        l->setVisible(compact_);
    resized();
    refreshFreqFields();
}

void BandStrip::setNumBands(int n)
{
    numBands_ = n;
    refreshFreqFields();
}

void BandStrip::setDimmed(bool dim)
{
    if (dim == dimmed_)
        return;
    dimmed_ = dim;
    inner_.setAlpha(dim ? 0.45f : 1.0f);
}

void BandStrip::refreshFreqFields()
{
    if (!compact_)
        return;
    const auto hz = xoverValues();
    const float lo = index_ == 0 ? xover::minFreq : hz[static_cast<size_t>(index_ - 1)];
    const float hi = index_ >= numBands_ - 1 ? xover::maxFreq : hz[static_cast<size_t>(index_)];
    const bool loEdit = index_ > 0, hiEdit = index_ < numBands_ - 1;
    auto setField = [](juce::Label& l, const juce::String& text, bool editable)
    {
        if (l.getText() != text && !l.isBeingEdited())
            l.setText(text, juce::dontSendNotification);
        if (l.isEditable() != editable)
            l.setEditable(editable, editable, false);
        l.setAlpha(editable ? 1.0f : 0.6f);
        l.setColour(juce::Label::textColourId, editable ? pal(l).ink : pal(l).ink2);
        l.setColour(juce::Label::backgroundColourId, pal(l).bg0);
    };
    setField(from_, xover::formatShort(lo), loEdit);
    setField(to_, xover::formatShort(hi), hiEdit);
}

void BandStrip::commitFreq(bool isTo, const juce::String& text)
{
    // From of band i = xover[i-1]; To of band i = xover[i].
    const int xi = isTo ? index_ : index_ - 1;
    float hz = 0.0f;
    if (xi >= 0 && xi < 4 && xover::parseHz(text, hz))
    {
        const auto vals = xoverValues();
        hz = xover::clamp(xi, hz, vals, numBands_);
        if (auto* param = apvts_.getParameter(pid::xover[static_cast<size_t>(xi)]))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(hz));
            param->endChangeGesture();
        }
    }
    refreshFreqFields();
    const auto vals = xoverValues();
    (isTo ? to_ : from_)
        .setText(xover::formatShort(xi >= 0 && xi < 4 ? vals[static_cast<size_t>(xi)]
                                                      : (isTo ? xover::maxFreq : xover::minFreq)),
                 juce::dontSendNotification);
}

void BandStrip::tick(float dt)
{
    const float peak = meters_.bandPeakDb[static_cast<size_t>(index_)].load(std::memory_order_relaxed);
    meter_.update(peak, dt);
    transient_.setGainDb(
        meters_.bandTransientGainDb[static_cast<size_t>(index_)].load(std::memory_order_relaxed));
    const float d = driveRaw_->load(std::memory_order_relaxed);
    if (d != lastDrive_)
    {
        lastDrive_ = d;
        curve_.setDriveDb(d);
    }
    if (compact_)
        refreshFreqFields();
}

void BandStrip::resized()
{
    auto b = getLocalBounds();
    b.removeFromLeft(3);
    b = b.withTrimmedLeft(5).withTrimmedRight(8).withTrimmedTop(8).withTrimmedBottom(6);

    auto head = b.removeFromTop(20);
    bypass_.setBounds(head.removeFromRight(20));
    head.removeFromRight(2);
    solo_.setBounds(head.removeFromRight(20));
    head.removeFromRight(2);
    mute_.setBounds(head.removeFromRight(20));
    b.removeFromTop(6);

    if (compact_)
    {
        auto row = b.removeFromTop(22);
        const int field = juce::jmin(68, (row.getWidth() - 40) / 2);
        fromCaption_.setBounds(row.removeFromLeft(30));
        from_.setBounds(row.removeFromLeft(field).reduced(0, 1));
        row.removeFromLeft(6);
        toCaption_.setBounds(row.removeFromLeft(18));
        to_.setBounds(row.removeFromLeft(field).reduced(0, 1));
        b.removeFromTop(6);
    }

    inner_.setBounds(b);
    auto r = inner_.getLocalBounds();

    const int sec1H = 6 + 16 + 4 + kKnobCellH + 5;
    const int sec3H = 6 + 70 + 5;
    const int baseSec2 = 6 + 22 + 4 + 44 + 4 + kKnobCellH + 5;
    const int extra = juce::jmax(0, r.getHeight() - sec1H - sec3H - baseSec2 - 12);
    const int curveExtra = juce::jmin(extra, 40);

    auto s1 = r.removeFromTop(sec1H);
    r.removeFromTop(6);
    auto s2 = r.removeFromTop(baseSec2 + curveExtra);
    r.removeFromTop(6);
    auto s3 = r.removeFromTop(sec3H);
    inner_.sections = {s1, s2, s3};
    inner_.repaint();

    // Section 1
    {
        auto a = s1.reduced(6, 0).withTrimmedTop(6).withTrimmedBottom(5);
        transient_.setBounds(a.removeFromTop(16));
        a.removeFromTop(4);
        const int cw = a.getWidth() / 3;
        attack_.setBounds(a.removeFromLeft(cw));
        sustain_.setBounds(a.removeFromLeft(cw));
        speed_.setBounds(a);
    }
    // Section 2
    {
        auto a = s2.reduced(6, 0).withTrimmedTop(6).withTrimmedBottom(5);
        sat_.setBounds(a.removeFromTop(22));
        a.removeFromTop(4);
        curve_.setBounds(a.removeFromTop(44 + curveExtra)
                             .withSizeKeepingCentre(juce::jmin(a.getWidth(), 152), 44 + curveExtra));
        a.removeFromTop(4);
        const int cw = a.getWidth() / 2;
        drive_.setBounds(a.removeFromLeft(cw));
        mix_.setBounds(a);
    }
    // Section 3
    {
        auto a = s3.reduced(6, 0).withTrimmedTop(6).withTrimmedBottom(5);
        gain_.setBounds(a.removeFromLeft(juce::jmin(a.getWidth() - 24, 84)));
        meter_.setBounds(juce::Rectangle<int>(10, 56).withCentre(a.getCentre()));
    }
}

void BandStrip::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    const auto c = p.band[static_cast<size_t>(index_)];
    g.setColour(p.bg1);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(c);
    g.fillRoundedRectangle(0.0f, 10.0f, 3.0f, static_cast<float>(getHeight() - 20), 1.5f);

    g.setColour(c);
    g.setFont(fonts::sans(12.5f, fonts::Weight::semiBold));
    g.drawText("B" + juce::String(index_ + 1), 11, 8, 40, 20, juce::Justification::centredLeft, false);
}
} // namespace pentagrit::ui
