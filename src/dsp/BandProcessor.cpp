// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/BandProcessor.h"
#include "dsp/DspUtils.h"

namespace pentagrit
{
namespace
{
constexpr double kRampSeconds = 0.005;
} // namespace

void BandProcessor::prepare(double procRate, int maxBlock, int numCh)
{
    const auto n = static_cast<size_t>(std::max(maxBlock, 1));
    dry_.setSize(std::max(numCh, 1), static_cast<int>(n), false, true, false);
    gainRamp_.assign(n, 1.0f);
    mixRamp_.assign(n, 1.0f);
    transient_.prepare(procRate, maxBlock, numCh);
    saturator_.prepare(procRate, maxBlock, numCh);
    setProcessingRate(procRate);
    reset();
}

void BandProcessor::setProcessingRate(double procRate) noexcept
{
    rate_ = procRate > 0.0 ? procRate : 44100.0;
    transient_.setProcessingRate(rate_);
    saturator_.setProcessingRate(rate_);
    gain_.reset(rate_, kRampSeconds);
    wetMix_.reset(rate_, kRampSeconds);
    needSnap_ = true;
}

void BandProcessor::reset() noexcept
{
    transient_.reset();
    saturator_.reset();
    gain_.setCurrentAndTargetValue(gain_.getTargetValue());
    wetMix_.setCurrentAndTargetValue(wetMix_.getTargetValue());
    needSnap_ = true;
    cold_ = false;
}

void BandProcessor::setSettings(const BandSettings& settings, bool audible, float lowFreqHintHz) noexcept
{
    settings_ = settings;
    audible_ = audible;
    transient_.setParameters(settings.attackDb, settings.sustainDb, settings.speed01);
    transient_.setLowFrequencyHint(lowFreqHintHz);
    saturator_.setType(settings.sat);
    saturator_.setDriveDb(settings.driveDb);

    const float g = audible ? dbToGain(settings.gainDb) : 0.0f;
    const float w = settings.bypass ? 0.0f : clampTo(settings.mix01, 0.0f, 1.0f);
    if (needSnap_)
    {
        gain_.setCurrentAndTargetValue(g);
        wetMix_.setCurrentAndTargetValue(w);
        needSnap_ = false;
    }
    else
    {
        gain_.setTargetValue(g);
        wetMix_.setTargetValue(w);
    }
}

void BandProcessor::process(const juce::dsp::AudioBlock<float>& blockIn) noexcept
{
    const int n = static_cast<int>(blockIn.getNumSamples());
    const int nc = std::min(static_cast<int>(blockIn.getNumChannels()), dry_.getNumChannels());
    if (n == 0 || nc == 0 || n > dry_.getNumSamples())
        return;
    auto block = blockIn.getSubsetChannelBlock(0, static_cast<size_t>(nc));

    const bool bypassSettled = !wetMix_.isSmoothing() && wetMix_.getTargetValue() == 0.0f;
    if (bypassSettled)
    {
        if (!cold_)
        {
            transient_.reset();
            saturator_.reset();
            cold_ = true;
        }
    }
    else
    {
        cold_ = false;
        const bool mixConst1 = !wetMix_.isSmoothing() && wetMix_.getTargetValue() >= 1.0f;
        if (!mixConst1)
            for (int ch = 0; ch < nc; ++ch)
                juce::FloatVectorOperations::copy(dry_.getWritePointer(ch),
                                                  block.getChannelPointer(static_cast<size_t>(ch)), n);

        transient_.process(block);
        saturator_.process(block);

        if (!mixConst1)
        {
            const bool smoothing = wetMix_.isSmoothing();
            const float mConst = wetMix_.getTargetValue();
            if (smoothing)
                for (int i = 0; i < n; ++i)
                    mixRamp_[static_cast<size_t>(i)] = wetMix_.getNextValue();
            for (int ch = 0; ch < nc; ++ch)
            {
                float* w = block.getChannelPointer(static_cast<size_t>(ch));
                const float* d = dry_.getReadPointer(ch);
                if (smoothing)
                    for (int i = 0; i < n; ++i)
                        w[i] = d[i] + (w[i] - d[i]) * mixRamp_[static_cast<size_t>(i)];
                else
                    for (int i = 0; i < n; ++i)
                        w[i] = d[i] + (w[i] - d[i]) * mConst;
            }
        }
    }

    // gain x audibility
    if (gain_.isSmoothing())
    {
        for (int i = 0; i < n; ++i)
            gainRamp_[static_cast<size_t>(i)] = gain_.getNextValue();
        for (int ch = 0; ch < nc; ++ch)
        {
            float* d = block.getChannelPointer(static_cast<size_t>(ch));
            for (int i = 0; i < n; ++i)
                d[i] *= gainRamp_[static_cast<size_t>(i)];
        }
    }
    else
    {
        const float g = gain_.getTargetValue();
        if (g != 1.0f)
            for (int ch = 0; ch < nc; ++ch)
                juce::FloatVectorOperations::multiply(block.getChannelPointer(static_cast<size_t>(ch)), g, n);
    }
}

float BandProcessor::getTransientGainDb() const noexcept
{
    return transient_.getLastGainDb();
}
} // namespace pentagrit
