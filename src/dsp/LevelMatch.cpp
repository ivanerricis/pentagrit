// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/LevelMatch.h"
#include "dsp/DspUtils.h"

#include <cmath>

namespace pentagrit
{
namespace
{
constexpr double kTrackerTauSec = 2.0;
constexpr double kGainRampSec = 0.050;
constexpr float kGatePower = 1.0e-6f; // -60 dBFS, as mean square

// Channel-averaged mean square of a block.
float meanSquare(const float* const* data, int numChannels, int numSamples) noexcept
{
    double sum = 0.0;
    for (int c = 0; c < numChannels; ++c)
    {
        const float* d = data[c];
        float acc = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            acc += d[i] * d[i];
        }
        sum += static_cast<double>(acc);
    }
    return static_cast<float>(sum / (static_cast<double>(numChannels) * numSamples));
}
} // namespace

void LevelMatch::prepare(double sampleRate) noexcept
{
    rate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    tau_ = timeToCoeff(kTrackerTauSec, rate_);
    gain_.reset(rate_, kGainRampSec);
    reset();
}

void LevelMatch::reset() noexcept
{
    primed_ = false;
    meanOut_ = meanRef_ = 0.0f;
    targetDb_ = 0.0f;
    gain_.setCurrentAndTargetValue(1.0f);
}

void LevelMatch::setActive(bool on, bool immediate) noexcept
{
    if (on == active_ && !(immediate && !on))
    {
        return;
    }
    active_ = on;
    if (on)
    {
        gain_.setTargetValue(dbToGain(targetDb_));
    }
    else if (immediate)
    {
        gain_.setCurrentAndTargetValue(1.0f);
    }
    else
    {
        gain_.setTargetValue(1.0f);
    }
}

void LevelMatch::analyse(const float* const* out, const float* const* ref, int numChannels,
                         int numSamples) noexcept
{
    if (numChannels <= 0 || numSamples <= 0)
    {
        return;
    }
    const float blockOut = meanSquare(out, numChannels, numSamples);
    const float blockRef = meanSquare(ref, numChannels, numSamples);
    if (!(blockOut > kGatePower) || !(blockRef > kGatePower)) // also rejects NaN
    {
        return;
    }

    if (!primed_)
    {
        meanOut_ = blockOut;
        meanRef_ = blockRef;
        primed_ = true;
    }
    else
    {
        // The same one-pole as running the pole over every sample of the block with its mean square.
        const float a = std::pow(tau_, static_cast<float>(numSamples));
        meanOut_ = blockOut + (meanOut_ - blockOut) * a;
        meanRef_ = blockRef + (meanRef_ - blockRef) * a;
    }

    const float db = 10.0f * std::log10(meanOut_ / meanRef_);
    targetDb_ = std::isfinite(db) ? clampTo(db, -kMaxDb, kMaxDb) : 0.0f;
    if (active_)
    {
        gain_.setTargetValue(dbToGain(targetDb_));
    }
}

void LevelMatch::advance(int numSamples) noexcept
{
    if (gain_.isSmoothing())
    {
        gain_.skip(numSamples);
    }
}

void LevelMatch::apply(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (gain_.isSmoothing())
    {
        // One ramp value per sample, shared by all channels.
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gain_.getNextValue();
            for (int c = 0; c < numChannels; ++c)
            {
                channels[c][i] *= g;
            }
        }
    }
    else if (const float g = gain_.getTargetValue(); g != 1.0f)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            juce::FloatVectorOperations::multiply(channels[c], g, numSamples);
        }
    }
}
} // namespace pentagrit
