// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/MultibandEngine.h"
#include "dsp/DspUtils.h"

#include <cmath>

namespace pentagrit
{
namespace
{
constexpr double kGainRampSec = 0.005;
constexpr double kBypassRampSec = 0.020;
constexpr double kMaxRate = 384000.0;

using Block = juce::dsp::AudioBlock<float>;

// Fills dst[0..n) with the next smoothed values; returns false (dst untouched) if the smoother is settled.
bool fillRamp(juce::SmoothedValue<float>& s, float* dst, int n) noexcept
{
    if (!s.isSmoothing())
        return false;
    for (int i = 0; i < n; ++i)
        dst[i] = s.getNextValue();
    return true;
}

// block *= smoothed gain
void applyGain(juce::SmoothedValue<float>& s, std::vector<float>& ramp, const Block& block) noexcept
{
    const int n = static_cast<int>(block.getNumSamples());
    const int nc = static_cast<int>(block.getNumChannels());
    if (fillRamp(s, ramp.data(), n))
    {
        for (int ch = 0; ch < nc; ++ch)
        {
            float* d = block.getChannelPointer(static_cast<size_t>(ch));
            for (int i = 0; i < n; ++i)
                d[i] *= ramp[static_cast<size_t>(i)];
        }
    }
    else if (const float g = s.getTargetValue(); g != 1.0f)
    {
        for (int ch = 0; ch < nc; ++ch)
            juce::FloatVectorOperations::multiply(block.getChannelPointer(static_cast<size_t>(ch)), g, n);
    }
}

// dst = dry + (dst - dry) * w, where w is the smoothed value (or the constant target if settled).
void blendWithDry(juce::SmoothedValue<float>& s, std::vector<float>& ramp, const Block& dst,
                  const Block& dry) noexcept
{
    const int n = static_cast<int>(dst.getNumSamples());
    const int nc = static_cast<int>(dst.getNumChannels());
    const bool smoothing = fillRamp(s, ramp.data(), n);
    const float w = s.getTargetValue();
    if (!smoothing && w >= 1.0f)
        return;
    for (int ch = 0; ch < nc; ++ch)
    {
        float* o = dst.getChannelPointer(static_cast<size_t>(ch));
        const float* d = dry.getChannelPointer(static_cast<size_t>(ch));
        if (smoothing)
            for (int i = 0; i < n; ++i)
                o[i] = d[i] + (o[i] - d[i]) * ramp[static_cast<size_t>(i)];
        else
            for (int i = 0; i < n; ++i)
                o[i] = d[i] + (o[i] - d[i]) * w;
    }
}

float peakOf(const Block& block) noexcept
{
    float peak = 0.0f;
    for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
    {
        const float* d = block.getChannelPointer(ch);
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const float v = std::abs(d[i]);
            if (v > peak)
                peak = v;
        }
    }
    return peak;
}

size_t factorIndex(int factor) noexcept
{
    return factor == 2 ? 0 : (factor == 4 ? 1 : 2);
}
} // namespace

void MultibandEngine::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlock_ = std::max(maxBlockSize, 1);
    numCh_ = std::max(numChannels, 1);
    maxFactor_ = effectiveFactor(OSFactor::x8, sampleRate_);
    prepared_ = false;

    for (size_t i = 0; i < 3; ++i)
    {
        fir_[i].reset();
        iir_[i].reset();
        if ((2 << i) > maxFactor_)
            continue;
        const auto stages = i + 1;
        fir_[i] = std::make_unique<Oversampler>(static_cast<size_t>(numCh_), stages,
                                                Oversampler::filterHalfBandFIREquiripple, true, true);
        iir_[i] = std::make_unique<Oversampler>(static_cast<size_t>(numCh_), stages,
                                                Oversampler::filterHalfBandPolyphaseIIR, true, false);
        fir_[i]->initProcessing(static_cast<size_t>(maxBlock_));
        iir_[i]->initProcessing(static_cast<size_t>(maxBlock_));
    }

    int maxLatency = 0;
    for (int f = 1; f <= maxFactor_; f *= 2)
        for (auto p : {OSPhase::linear, OSPhase::minimum})
            maxLatency = std::max(maxLatency, computeLatency(f, p));
    int ringSize = 64;
    while (ringSize < maxLatency + 2)
        ringSize <<= 1;
    delayLine_.setSize(numCh_, ringSize, false, true, false);
    delayMask_ = ringSize - 1;
    delayPos_ = 0;
    delayed_.setSize(numCh_, maxBlock_, false, true, false);

    const int maxProc = maxBlock_ * maxFactor_;
    dryProc_.setSize(numCh_, maxProc, false, true, false);
    for (auto& b : bandBuf_)
        b.setSize(numCh_, maxProc, false, true, false);
    ramp_.assign(static_cast<size_t>(maxProc), 1.0f);

    crossover_.prepare(sampleRate_, sampleRate_, maxProc, numCh_);
    for (auto& b : bands_)
        b.prepare(sampleRate_, maxProc, numCh_);
    autoGain_.prepare(sampleRate_, maxProc, numCh_);

    outGain_.reset(sampleRate_, kGainRampSec);
    bypassW_.reset(sampleRate_, kBypassRampSec);
    inGain_.reset(sampleRate_, kGainRampSec);
    mix_.reset(sampleRate_, kGainRampSec);

    prepared_ = true;
    configured_ = false;
    needSnap_ = true;
    coreCold_ = false;
    factor_ = 1;
    numBands_ = 0;
    setSettings(settings_);
    reset(); // leaves needSnap_ set: the first host settings after prepare() are applied without ramps
    setSettings(settings_);
    needSnap_ = true;
}

void MultibandEngine::reset() noexcept
{
    if (!prepared_)
        return;
    resetCore();
    delayLine_.clear();
    delayPos_ = 0;
    needSnap_ = true;
    coreCold_ = false;
}

void MultibandEngine::resetCore() noexcept
{
    if (auto* os = activeOversampler())
        os->reset();
    crossover_.reset();
    for (auto& b : bands_)
        b.reset();
    autoGain_.reset();
}

MultibandEngine::Oversampler* MultibandEngine::activeOversampler() noexcept
{
    if (factor_ <= 1)
        return nullptr;
    const auto idx = factorIndex(factor_);
    return phase_ == OSPhase::linear ? fir_[idx].get() : iir_[idx].get();
}

int MultibandEngine::computeLatency(int factor, OSPhase phase) const noexcept
{
    if (factor <= 1)
        return 0;
    const auto idx = factorIndex(factor);
    const auto* os = phase == OSPhase::linear ? fir_[idx].get() : iir_[idx].get();
    if (os == nullptr)
        return 0;
    return std::max(0, static_cast<int>(std::ceil(os->getLatencyInSamples() - 1.0e-3f)));
}

void MultibandEngine::applyConfiguration() noexcept
{
    const auto& s = settings_;
    const OSFactor requested = (nonRealtime_ && !s.osRenderSame) ? s.osRender : s.os;
    const int factor = std::min(effectiveFactor(requested, sampleRate_), maxFactor_);
    const int numBands = std::clamp(s.numBands, 2, kMaxBands);

    if (!configured_ || factor != factor_ || s.osPhase != phase_)
    {
        factor_ = factor;
        phase_ = s.osPhase;
        latency_ = computeLatency(factor_, phase_);
        const double rate = sampleRate_ * factor_;
        crossover_.setProcessingRate(rate);
        for (auto& b : bands_)
            b.setProcessingRate(rate);
        autoGain_.setProcessingRate(rate);
        inGain_.reset(rate, kGainRampSec);
        mix_.reset(rate, kGainRampSec);
        resetCore();
    }

    if (!configured_ || numBands != numBands_)
    {
        numBands_ = numBands;
        crossover_.setNumBands(numBands_);
        for (auto& b : bands_)
            b.reset();
    }
    configured_ = true;
}

void MultibandEngine::setNonRealtime(bool nonRealtime) noexcept
{
    if (nonRealtime == nonRealtime_)
        return;
    nonRealtime_ = nonRealtime;
    if (prepared_)
        setSettings(settings_);
}

void MultibandEngine::setSettings(const EngineSettings& settings) noexcept
{
    settings_ = settings;
    if (!prepared_)
        return;
    applyConfiguration();

    crossover_.setFrequencies(settings_.xoverHz);
    const auto eff = crossover_.getEffectiveFrequencies();
    const auto audible = computeAudibility(settings_);
    for (int b = 0; b < numBands_; ++b)
    {
        const float hint = b == 0 ? 30.0f : eff[static_cast<size_t>(b - 1)];
        bands_[static_cast<size_t>(b)].setSettings(settings_.bands[static_cast<size_t>(b)],
                                                   audible[static_cast<size_t>(b)], hint);
    }
    autoGain_.setEnabled(settings_.autoGain);

    const float inG = dbToGain(settings_.inGainDb), outG = dbToGain(settings_.outGainDb);
    const float mixV = clampTo(settings_.mix01, 0.0f, 1.0f);
    const float byp = settings_.bypass ? 1.0f : 0.0f;
    if (needSnap_)
    {
        inGain_.setCurrentAndTargetValue(inG);
        outGain_.setCurrentAndTargetValue(outG);
        mix_.setCurrentAndTargetValue(mixV);
        bypassW_.setCurrentAndTargetValue(byp);
        needSnap_ = false;
    }
    else
    {
        inGain_.setTargetValue(inG);
        outGain_.setTargetValue(outG);
        mix_.setTargetValue(mixV);
        bypassW_.setTargetValue(byp);
    }

    for (size_t i = 0; i < 4; ++i)
        meters_.effectiveXoverHz[i].store(eff[i], std::memory_order_relaxed);
    meters_.latencySamples.store(latency_, std::memory_order_relaxed);
}

int MultibandEngine::getLatencySamples() const noexcept
{
    return latency_;
}

void MultibandEngine::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const int total = buffer.getNumSamples();
    const int nc = std::min(buffer.getNumChannels(), numCh_);
    if (!prepared_ || total <= 0 || nc <= 0)
        return;

    juce::ScopedNoDenormals noDenormals;
    for (int pos = 0; pos < total; pos += maxBlock_)
        processChunk(buffer, pos, std::min(maxBlock_, total - pos), nc);

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto i = static_cast<size_t>(b);
        if (b < numBands_)
        {
            meters_.bandTransientGainDb[i].store(bands_[i].getTransientGainDb(), std::memory_order_relaxed);
        }
        else
        {
            meters_.bandPeakDb[i].store(kSilenceDb, std::memory_order_relaxed);
            meters_.bandTransientGainDb[i].store(0.0f, std::memory_order_relaxed);
        }
    }
    meters_.autoGainDb.store(autoGain_.getCurrentGainDb(), std::memory_order_relaxed);
}

void MultibandEngine::processChunk(juce::AudioBuffer<float>& buffer, int start, int n, int nc) noexcept
{
    Block base = Block(buffer)
                     .getSubBlock(static_cast<size_t>(start), static_cast<size_t>(n))
                     .getSubsetChannelBlock(0, static_cast<size_t>(nc));

    auto storePeak = [start](std::atomic<float>& meter, float db)
    {
        // Peak over the whole host block: max across chunks, restarted on the first chunk.
        meter.store(start == 0 ? db : std::max(db, meter.load(std::memory_order_relaxed)),
                    std::memory_order_relaxed);
    };

    storePeak(meters_.inPeakDb, gainToDb(peakOf(base)));

    // Integer delay of the dry input (bypass path); always running so the history is warm.
    const int latency = latency_;
    for (int ch = 0; ch < nc; ++ch)
    {
        float* ring = delayLine_.getWritePointer(ch);
        const float* in = base.getChannelPointer(static_cast<size_t>(ch));
        float* out = delayed_.getWritePointer(ch);
        int w = delayPos_;
        for (int i = 0; i < n; ++i)
        {
            ring[w & delayMask_] = in[i];
            out[i] = ring[(w - latency) & delayMask_];
            ++w;
        }
    }
    delayPos_ = (delayPos_ + n) & delayMask_;

    const bool bypassFull = !bypassW_.isSmoothing() && bypassW_.getTargetValue() >= 1.0f;
    if (bypassFull)
    {
        if (!coreCold_)
        {
            resetCore();
            coreCold_ = true;
        }
        for (int ch = 0; ch < nc; ++ch)
            juce::FloatVectorOperations::copy(base.getChannelPointer(static_cast<size_t>(ch)),
                                              delayed_.getReadPointer(ch), n);
        for (auto& p : meters_.bandPeakDb)
            p.store(kSilenceDb, std::memory_order_relaxed);
        storePeak(meters_.outPeakDb, gainToDb(peakOf(base)));
        return;
    }
    coreCold_ = false;

    // ---- oversampled core ----
    Oversampler* os = activeOversampler();
    Block work = base;
    if (os != nullptr)
        work = os->processSamplesUp(juce::dsp::AudioBlock<const float>(base))
                   .getSubsetChannelBlock(0, static_cast<size_t>(nc));
    const size_t m = work.getNumSamples();

    Block dry = Block(dryProc_).getSubBlock(0, m).getSubsetChannelBlock(0, static_cast<size_t>(nc));
    dry.copyFrom(work);
    crossover_.processAllpassChain(dry);

    applyGain(inGain_, ramp_, work);

    Crossover::BandBlocks blocks;
    for (int b = 0; b < numBands_; ++b)
        blocks[static_cast<size_t>(b)] = Block(bandBuf_[static_cast<size_t>(b)])
                                             .getSubBlock(0, m)
                                             .getSubsetChannelBlock(0, static_cast<size_t>(nc));
    crossover_.split(juce::dsp::AudioBlock<const float>(work), blocks);

    work.clear();
    for (int b = 0; b < numBands_; ++b)
    {
        auto& blk = blocks[static_cast<size_t>(b)];
        bands_[static_cast<size_t>(b)].process(blk);
        storePeak(meters_.bandPeakDb[static_cast<size_t>(b)], gainToDb(peakOf(blk)));
        work.add(blk);
    }

    autoGain_.process(juce::dsp::AudioBlock<const float>(dry), work);
    blendWithDry(mix_, ramp_, work, dry);

    if (os != nullptr)
        os->processSamplesDown(base);

    applyGain(outGain_, ramp_, base);

    // Bypass crossfade against the delayed dry: out = wet * (1 - w) + dry * w.
    {
        const bool smoothing = fillRamp(bypassW_, ramp_.data(), n);
        const float wc = bypassW_.getTargetValue();
        if (smoothing || wc > 0.0f)
            for (int ch = 0; ch < nc; ++ch)
            {
                float* o = base.getChannelPointer(static_cast<size_t>(ch));
                const float* d = delayed_.getReadPointer(ch);
                if (smoothing)
                    for (int i = 0; i < n; ++i)
                        o[i] += (d[i] - o[i]) * ramp_[static_cast<size_t>(i)];
                else
                    for (int i = 0; i < n; ++i)
                        o[i] += (d[i] - o[i]) * wc;
            }
    }
    storePeak(meters_.outPeakDb, gainToDb(peakOf(base)));
}

int MultibandEngine::effectiveFactor(OSFactor requested, double sampleRate) noexcept
{
    int factor = 1 << static_cast<int>(requested);
    while (factor > 1 && sampleRate * factor > kMaxRate)
        factor /= 2;
    return factor;
}

std::array<bool, kMaxBands> MultibandEngine::computeAudibility(const EngineSettings& s) noexcept
{
    std::array<bool, kMaxBands> audible{};
    const int nb = std::clamp(s.numBands, 0, kMaxBands);
    bool anySolo = false;
    for (int b = 0; b < nb; ++b)
        anySolo = anySolo || s.bands[static_cast<size_t>(b)].solo;

    for (int b = 0; b < nb; ++b)
    {
        const auto& band = s.bands[static_cast<size_t>(b)];
        audible[static_cast<size_t>(b)] = anySolo ? band.solo : !band.mute;
    }
    return audible;
}
} // namespace pentagrit
