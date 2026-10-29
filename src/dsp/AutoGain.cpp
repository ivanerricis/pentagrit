// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/AutoGain.h"
#include "dsp/DspUtils.h"

namespace pentagrit
{
namespace
{
constexpr double kPowerTauSec = 0.400;
constexpr double kGainTauSec = 0.200;
constexpr float kMaxDb = 24.0f;
constexpr float kRefFloor = 1.0e-6f;
} // namespace

void AutoGain::prepare(double procRate, int, int)
{
    setProcessingRate(procRate);
    reset();
}

void AutoGain::setProcessingRate(double procRate) noexcept
{
    rate_ = procRate > 0.0 ? procRate : 44100.0;
    powCoeff_ = timeToCoeff(kPowerTauSec, rate_);
    gainCoeff_ = timeToCoeff(kGainTauSec, rate_);
}

void AutoGain::reset() noexcept
{
    currentGainDb_ = 0.0f;
    meanRef_ = meanWet_ = 0.0f;
    targetDb_ = 0.0f;
}

void AutoGain::setEnabled(bool enabled) noexcept
{
    enabled_ = enabled;
}

float AutoGain::getCurrentGainDb() const noexcept
{
    return currentGainDb_;
}

void AutoGain::process(const juce::dsp::AudioBlock<const float>& ref,
                       const juce::dsp::AudioBlock<float>& wet) noexcept
{
    const auto n = wet.getNumSamples();
    const auto wetCh = wet.getNumChannels();
    const auto refCh = ref.getNumChannels();
    if (n == 0 || wetCh == 0)
        return;

    const float invRefCh = refCh > 0 ? 1.0f / static_cast<float>(refCh) : 0.0f;
    const float invWetCh = 1.0f / static_cast<float>(wetCh);
    const auto refN = std::min(n, ref.getNumSamples());

    // Measurement always runs so enabling does not start from an empty window.
    float mRef = meanRef_, mWet = meanWet_;
    for (size_t i = 0; i < n; ++i)
    {
        float r = 0.0f, w = 0.0f;
        if (i < refN)
            for (size_t c = 0; c < refCh; ++c)
            {
                const float v = ref.getSample(static_cast<int>(c), static_cast<int>(i));
                r += v * v;
            }
        for (size_t c = 0; c < wetCh; ++c)
        {
            const float v = wet.getSample(static_cast<int>(c), static_cast<int>(i));
            w += v * v;
        }
        r *= invRefCh;
        w *= invWetCh;
        mRef = r + powCoeff_ * (mRef - r);
        mWet = w + powCoeff_ * (mWet - w);
    }
    if (mRef < 1.0e-20f)
        mRef = 0.0f;
    if (mWet < 1.0e-20f)
        mWet = 0.0f;
    meanRef_ = mRef;
    meanWet_ = mWet;

    if (mRef > kRefFloor)
        targetDb_ = clampTo(10.0f * std::log10(mRef / (mWet + 1.0e-12f)), -kMaxDb, kMaxDb);
    // else: hold the previous target

    const float goal = enabled_ ? targetDb_ : 0.0f;
    const float startDb = currentGainDb_;
    // Closed-form block advance of the one-pole: g_n = goal + c^n (g_0 - goal).
    const float cN = std::pow(gainCoeff_, static_cast<float>(n));
    float endDb = goal + cN * (startDb - goal);
    if (!enabled_ && std::abs(endDb) < 1.0e-3f)
        endDb = 0.0f;
    currentGainDb_ = endDb;

    if (startDb == 0.0f && endDb == 0.0f)
        return;

    const float g0 = dbToGain(startDb), g1 = dbToGain(endDb);
    const float step = (g1 - g0) / static_cast<float>(n);
    for (size_t c = 0; c < wetCh; ++c)
    {
        auto* d = wet.getChannelPointer(c);
        float g = g0;
        for (size_t i = 0; i < n; ++i)
        {
            g += step;
            d[i] *= g;
        }
    }
}
} // namespace pentagrit
