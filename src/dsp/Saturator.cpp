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
void shapeBlock(SatType type, const float* u, float* s, int n) noexcept
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
    float gain[kChunk], u[kChunk], s[kChunk];

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int len = std::min(kChunk, numSamples - start);

        // Drive smoothing and crossfade position are shared by all channels.
        for (int i = 0; i < len; ++i)
        {
            g = targetGain_ + driveCoeff_ * (g - targetGain_);
            gain[i] = g;
        }
        const bool chunkFading = fp < fadeLen_;

        for (size_t ch = 0; ch < numCh; ++ch)
        {
            float* d = block.getChannelPointer(ch) + start;
            float x1 = dcX1_[ch], y1 = dcY1_[ch];

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
                    if (p < fadeLen_)
                    {
                        const float t = static_cast<float>(p + 1) * invFade;
                        const float a = prevType_ == SatType::off ? d[i] : sPrev[static_cast<size_t>(i)];
                        const float b = type_ == SatType::off ? d[i] : sCur[static_cast<size_t>(i)];
                        s[i] = a * (1.0f - t) + b * t;
                    }
                    else
                    {
                        s[i] = type_ == SatType::off ? d[i] : sCur[static_cast<size_t>(i)];
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

            const float dc = dcCoeff_;
            for (int i = 0; i < len; ++i)
            {
                float y = s[i] - x1 + dc * y1;
                if (std::abs(y) < 1.0e-20f)
                    y = 0.0f;
                x1 = s[i];
                y1 = y;
                d[i] = y;
            }
            dcX1_[ch] = x1;
            dcY1_[ch] = y1;
        }
        if (chunkFading)
            fp = std::min(fp + len, fadeLen_);
    }
    smoothGain_ = g;
    fadePos_ = fp;
}
} // namespace pentagrit
