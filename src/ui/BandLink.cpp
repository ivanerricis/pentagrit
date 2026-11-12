// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/BandLink.h"

namespace pentagrit::ui
{
namespace
{
// The linked parameters; satType (index 4) is copied as is, the others move by the same offset.
constexpr std::array<BandParam, 7> kLinkedParams{BandParam::gain,  BandParam::attack,  BandParam::sustain,
                                                 BandParam::speed, BandParam::satType, BandParam::drive,
                                                 BandParam::mix};
constexpr int kSatTypeKind = 4;

float clamp01(float v) noexcept
{
    return juce::jlimit(0.0f, 1.0f, v);
}
} // namespace

const juce::Identifier BandLink::maskProperty{"bandLinkMask"};

BandLink::BandLink(juce::AudioProcessorValueTreeState& apvts, juce::UndoManager& undo)
    : apvts_(apvts), undo_(undo)
{
    int maxIndex = 0;
    for (auto* p : apvts_.processor.getParameters())
    {
        maxIndex = juce::jmax(maxIndex, p->getParameterIndex());
    }
    slots_.assign(static_cast<size_t>(maxIndex) + 1, Slot{});
    for (int band = 0; band < kMaxBands; ++band)
    {
        for (size_t k = 0; k < kLinkedParams.size(); ++k)
        {
            if (auto* p = apvts_.getParameter(bandParamID(band, kLinkedParams[k])))
            {
                slots_[static_cast<size_t>(p->getParameterIndex())] = {band, static_cast<int>(k)};
                p->addListener(this);
                listened_.push_back(p);
            }
        }
    }
}

BandLink::~BandLink()
{
    for (auto* p : listened_)
    {
        p->removeListener(this);
    }
}

int BandLink::mask() const
{
    return static_cast<int>(apvts_.state.getProperty(maskProperty, 0)) & ((1 << kMaxBands) - 1);
}

bool BandLink::isLinked(int band) const
{
    return band >= 0 && band < kMaxBands && ((mask() >> band) & 1) != 0;
}

void BandLink::setLinked(int band, bool linked)
{
    if (band < 0 || band >= kMaxBands || isLinked(band) == linked)
    {
        return;
    }
    const int m = linked ? (mask() | (1 << band)) : (mask() & ~(1 << band));
    apvts_.state.setProperty(maskProperty, m, nullptr); // not undoable, not a parameter
}

bool BandLink::numBandsActive(int band) const
{
    const int n =
        juce::jlimit(2, kMaxBands, juce::roundToInt(apvts_.getRawParameterValue(pid::numBands)->load()));
    return band < n;
}

void BandLink::parameterGestureChanged(int index, bool starting)
{
    if (guard_ || index < 0 || static_cast<size_t>(index) >= slots_.size() ||
        !juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return;
    }
    const auto slot = slots_[static_cast<size_t>(index)];

    if (starting)
    {
        // Undo and redo write through gestures too; they restore every band themselves.
        if (leader_ >= 0 || slot.band < 0 || undo_.isPerformingUndoRedo() || !isLinked(slot.band) ||
            !numBandsActive(slot.band))
        {
            return;
        }
        auto* leader =
            apvts_.getParameter(bandParamID(slot.band, kLinkedParams[static_cast<size_t>(slot.kind)]));
        numSiblings_ = 0;
        for (int b = 0; b < kMaxBands; ++b)
        {
            if (b == slot.band || !isLinked(b) || !numBandsActive(b))
            {
                continue;
            }
            if (auto* p = apvts_.getParameter(bandParamID(b, kLinkedParams[static_cast<size_t>(slot.kind)])))
            {
                siblings_[static_cast<size_t>(numSiblings_++)] = {p, p->getValue()};
            }
        }
        if (numSiblings_ == 0)
        {
            return;
        }
        leader_ = index;
        leaderKind_ = slot.kind;
        leaderStart_ = leader->getValue();

        const juce::ScopedValueSetter<bool> g(guard_, true);
        for (int i = 0; i < numSiblings_; ++i)
        {
            siblings_[static_cast<size_t>(i)].param->beginChangeGesture();
        }
    }
    else if (index == leader_)
    {
        const juce::ScopedValueSetter<bool> g(guard_, true);
        for (int i = 0; i < numSiblings_; ++i)
        {
            siblings_[static_cast<size_t>(i)].param->endChangeGesture();
        }
        leader_ = -1;
        numSiblings_ = 0;
    }
}

void BandLink::parameterValueChanged(int index, float newValue)
{
    if (guard_ || index != leader_ || !juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return;
    }
    const juce::ScopedValueSetter<bool> g(guard_, true);
    const float delta = newValue - leaderStart_;
    for (int i = 0; i < numSiblings_; ++i)
    {
        const auto& s = siblings_[static_cast<size_t>(i)];
        s.param->setValueNotifyingHost(leaderKind_ == kSatTypeKind ? newValue : clamp01(s.start + delta));
    }
}
} // namespace pentagrit::ui
