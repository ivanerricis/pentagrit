// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/Saturator.h"
#include "dsp/DspUtils.h"

#include <array>

namespace pentagrit
{
namespace
{
constexpr double kDriveSmoothSec = 0.020;
constexpr double kFadeSec = 0.010;
constexpr double kDcHz = 5.0;

const float kTubeOffset = fastTanh(0.25f);
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
            return fastTanh(u);
        case SatType::softClip:
        {
            const float v = clampTo(u, -1.0f, 1.0f);
            return 1.5f * (v - v * v * v * (1.0f / 3.0f));
        }
        case SatType::tube:
            return fastTanh(u + 0.25f) - kTubeOffset;
        case SatType::wavefold:
            return fastSinHalfPi(u);
        case SatType::hardClip:
            return clampTo(u, -1.0f, 1.0f);
    }
    return u;
}

namespace
{
constexpr int kChunk = 256;

// Vectorizable block version of Saturator::shape (u -> s), same maths.
void shapeBlock(SatType type, const float* __restrict u, float* __restrict s, int n) noexcept
{
    switch (type)
    {
        case SatType::off:
            std::copy(u, u + n, s);
            break;
        case SatType::tanh:
            for (int i = 0; i < n; ++i)
                s[i] = fastTanh(u[i]);
            break;
        case SatType::softClip:
            for (int i = 0; i < n; ++i)
            {
                const float v = clampTo(u[i], -1.0f, 1.0f);
                s[i] = 1.5f * (v - v * v * v * (1.0f / 3.0f));
            }
            break;
        case SatType::tube:
            for (int i = 0; i < n; ++i)
                s[i] = fastTanh(u[i] + 0.25f) - kTubeOffset;
            break;
        case SatType::wavefold:
            for (int i = 0; i < n; ++i)
                s[i] = fastSinHalfPi(u[i]);
            break;
        case SatType::hardClip:
            for (int i = 0; i < n; ++i)
                s[i] = clampTo(u[i], -1.0f, 1.0f);
            break;
    }
}
} // namespace

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
    const auto numSamples = static_cast<int>(block.getNumSamples());
    if (numCh == 0 || numSamples == 0)
        return;

    const float invFade = 1.0f / static_cast<float>(fadeLen_);
    float g = smoothGain_;
    int fp = fadePos_;
    float gain[kChunk], u[kChunk], sb[2][kChunk];

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int len = std::min(kChunk, numSamples - start);

        // Drive smoothing and crossfade position are shared by all channels. Once the smoother has settled on
        // the target the gain is constant and the recursion is skipped.
        const bool constGain = g == targetGain_;
        if (!constGain)
            for (int i = 0; i < len; ++i)
            {
                g = targetGain_ + driveCoeff_ * (g - targetGain_);
                gain[i] = g;
            }
        const float tg = targetGain_;
        const bool chunkFading = fp < fadeLen_;

        // Channels are processed in pairs so the two serial DC-blocker recursions overlap.
        for (size_t ch0 = 0; ch0 < numCh; ch0 += 2)
        {
            const size_t np = std::min<size_t>(2, numCh - ch0);
            for (size_t k = 0; k < np; ++k)
            {
                float* d = block.getChannelPointer(ch0 + k) + start;
                float* s = sb[k];
                if (constGain)
                    for (int i = 0; i < len; ++i)
                        u[i] = d[i] * tg;
                else
                    for (int i = 0; i < len; ++i)
                        u[i] = d[i] * gain[i];

                if (chunkFading)
                {
                    std::array<float, kChunk> sPrev, sCur;
                    shapeBlock(prevType_, u, sPrev.data(), len);
                    shapeBlock(type_, u, sCur.data(), len);
                    for (int i = 0; i < len; ++i)
                    {
                        const int p = fp + i;
                        const float b = type_ == SatType::off ? d[i] : sCur[static_cast<size_t>(i)];
                        if (p < fadeLen_)
                        {
                            const float t = static_cast<float>(p + 1) * invFade;
                            const float a = prevType_ == SatType::off ? d[i] : sPrev[static_cast<size_t>(i)];
                            s[i] = a * (1.0f - t) + b * t;
                        }
                        else
                        {
                            s[i] = b;
                        }
                    }
                }
                else if (type_ == SatType::off)
                {
                    std::copy(d, d + len, s);
                }
                else
                {
                    shapeBlock(type_, u, s, len);
                }
            }

            // Branch-free DC blocker; denormals are flushed once per chunk (the pole is so slow that the
            // state cannot cross the denormal range within a chunk).
            const float dc = dcCoeff_;
            if (np == 2)
            {
                float* d0 = block.getChannelPointer(ch0) + start;
                float* d1 = block.getChannelPointer(ch0 + 1) + start;
                float xa = dcX1_[ch0], ya = dcY1_[ch0], xb = dcX1_[ch0 + 1], yb = dcY1_[ch0 + 1];
                for (int i = 0; i < len; ++i)
                {
                    const float sa = sb[0][i], sc = sb[1][i];
                    const float y0 = sa - xa + dc * ya;
                    const float y1 = sc - xb + dc * yb;
                    xa = sa;
                    xb = sc;
                    ya = y0;
                    yb = y1;
                    d0[i] = y0;
                    d1[i] = y1;
                }
                dcX1_[ch0] = std::abs(xa) < 1.0e-20f ? 0.0f : xa;
                dcY1_[ch0] = std::abs(ya) < 1.0e-20f ? 0.0f : ya;
                dcX1_[ch0 + 1] = std::abs(xb) < 1.0e-20f ? 0.0f : xb;
                dcY1_[ch0 + 1] = std::abs(yb) < 1.0e-20f ? 0.0f : yb;
            }
            else
            {
                float* d0 = block.getChannelPointer(ch0) + start;
                float xa = dcX1_[ch0], ya = dcY1_[ch0];
                for (int i = 0; i < len; ++i)
                {
                    const float sa = sb[0][i];
                    const float y0 = sa - xa + dc * ya;
                    xa = sa;
                    ya = y0;
                    d0[i] = y0;
                }
                dcX1_[ch0] = std::abs(xa) < 1.0e-20f ? 0.0f : xa;
                dcY1_[ch0] = std::abs(ya) < 1.0e-20f ? 0.0f : ya;
            }
        }
        if (chunkFading)
            fp = std::min(fp + len, fadeLen_);
    }
    smoothGain_ = g;
    fadePos_ = fp;
}
} // namespace pentagrit
