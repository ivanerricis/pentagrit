// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/Saturator.h"
#include "dsp/DspUtils.h"

namespace pentagrit
{
namespace
{
constexpr double kDriveSmoothSec = 0.020;
constexpr double kFadeSec = 0.010;
constexpr double kDcHz = 5.0;
constexpr float kPiHalf = 1.57079632679489662f;

// Output of one curve for input x with pre-gained input u. `off` is the dry signal (used while crossfading).
inline float curveOrDry(SatType t, float x, float u) noexcept
{
    return t == SatType::off ? x : Saturator::shape(t, u);
}
} // namespace

void Saturator::prepare(double procRate, int, int numCh)
{
    const auto n = static_cast<size_t>(std::max(numCh, 1));
    dcX1_.assign(n, 0.0f);
    dcY1_.assign(n, 0.0f);
    setProcessingRate(procRate);
    reset();
}

void Saturator::updateCoefficients() noexcept
{
    driveCoeff_ = timeToCoeff(kDriveSmoothSec, rate_);
    fadeLen_ = std::max(1, static_cast<int>(kFadeSec * rate_ + 0.5));
    fadePos_ = std::min(fadePos_, fadeLen_);
    dcCoeff_ = static_cast<float>(std::exp(-2.0 * 3.14159265358979323846 * kDcHz / rate_));
}

void Saturator::setProcessingRate(double procRate) noexcept
{
    rate_ = procRate > 0.0 ? procRate : 44100.0;
    updateCoefficients();
}

void Saturator::reset() noexcept
{
    std::fill(dcX1_.begin(), dcX1_.end(), 0.0f);
    std::fill(dcY1_.begin(), dcY1_.end(), 0.0f);
    smoothGain_ = targetGain_;
    fadePos_ = fadeLen_;
    prevType_ = type_;
}

void Saturator::setType(SatType type) noexcept
{
    if (type == type_)
        return;
    prevType_ = type_;
    type_ = type;
    fadePos_ = 0;
}

void Saturator::setDriveDb(float driveDb) noexcept
{
    driveDb_ = driveDb;
    targetGain_ = dbToGain(driveDb);
    if (driveSnap_)
    {
        smoothGain_ = targetGain_;
        driveSnap_ = false;
    }
}

float Saturator::shape(SatType type, float u) noexcept
{
    switch (type)
    {
    case SatType::off:
        return u;
    case SatType::tanh:
        return std::tanh(u);
    case SatType::softClip:
    {
        const float v = clampTo(u, -1.0f, 1.0f);
        return 1.5f * (v - v * v * v * (1.0f / 3.0f));
    }
    case SatType::tube:
    {
        static const float offset = std::tanh(0.25f);
        return std::tanh(u + 0.25f) - offset;
    }
    case SatType::wavefold:
        return std::sin(kPiHalf * u);
    case SatType::hardClip:
        return clampTo(u, -1.0f, 1.0f);
    }
    return u;
}

void Saturator::process(const juce::dsp::AudioBlock<float>& block) noexcept
{
    const bool fading = fadePos_ < fadeLen_;
    if (type_ == SatType::off && !fading)
    {
        // Fully transparent; drop DC blocker state so re-enabling starts clean.
        std::fill(dcX1_.begin(), dcX1_.end(), 0.0f);
        std::fill(dcY1_.begin(), dcY1_.end(), 0.0f);
        smoothGain_ = targetGain_;
        return;
    }

    const auto numCh = std::min(block.getNumChannels(), dcX1_.size());
    const auto numSamples = block.getNumSamples();
    if (numCh == 0 || numSamples == 0)
        return;

    const float startGain = smoothGain_;
    const int startFade = fadePos_;
    const float invFade = 1.0f / static_cast<float>(fadeLen_);

    for (size_t ch = 0; ch < numCh; ++ch)
    {
        auto* d = block.getChannelPointer(ch);
        float g = startGain;
        int fp = startFade;
        float x1 = dcX1_[ch], y1 = dcY1_[ch];

        for (size_t i = 0; i < numSamples; ++i)
        {
            g = targetGain_ + driveCoeff_ * (g - targetGain_);
            const float x = d[i];
            const float u = x * g;

            float s;
            if (fp < fadeLen_)
            {
                const float t = static_cast<float>(fp + 1) * invFade;
                s = curveOrDry(prevType_, x, u) * (1.0f - t) + curveOrDry(type_, x, u) * t;
                ++fp;
            }
            else
            {
                s = curveOrDry(type_, x, u);
            }

            float y = s - x1 + dcCoeff_ * y1;
            if (std::abs(y) < 1.0e-20f)
                y = 0.0f;
            x1 = s;
            y1 = y;
            d[i] = y;
        }
        dcX1_[ch] = x1;
        dcY1_[ch] = y1;
        if (ch == 0)
        {
            smoothGain_ = g;
            fadePos_ = fp;
        }
    }
}
} // namespace pentagrit
