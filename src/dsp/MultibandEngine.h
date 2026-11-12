// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/AutoGain.h"
#include "dsp/BandProcessor.h"
#include "dsp/Crossover.h"
#include "dsp/DspTypes.h"
#include "dsp/LevelMatch.h"
#include "dsp/Meters.h"
#include <array>
#include <memory>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace pentagrit
{
// Full signal chain (oversampling, crossover, bands, autogain, dry/wet, bypass). Owned by WP4.
//
// input -> [delay line for bypass] -> Oversampler.up -> (dry copy -> allpass chain) -> inGain -> crossover ->
// bands -> sum -> AutoGain (ref = phase-matched dry taken before inGain) -> global mix with the phase-matched
// dry [-> delta: wet - dry] -> Oversampler.down -> outGain -> bypass crossfade (against the delayed input,
// optionally level matched, see LevelMatch).
//
// Lookahead (EngineSettings::lookaheadMs): every band delays its audio by L base-rate samples (L * factor at
// the processing rate) while its transient detector sees the undelayed signal; the phase-matched dry is
// delayed by the same amount, so the band sum, the per-band mix, the global mix and the auto-gain reference
// stay aligned. The latency is the oversampler latency plus L, and the bypass delay line uses it as well.
class MultibandEngine
{
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    // Applies the settings immediately (factor/phase/numBands changes reconfigure and reset the core). Call
    // once per host block before process(); latency and meters reflect the new configuration straight away.
    void setSettings(const EngineSettings& settings) noexcept;
    // Offline rendering: when true and osRenderSame is false, the render factor replaces the live factor.
    void setNonRealtime(bool nonRealtime) noexcept;

    // Processes in place; large buffers are chunked internally.
    void process(juce::AudioBuffer<float>& buffer) noexcept;

    int getLatencySamples() const noexcept;

    // Oversampling factor actually used (1, 2, 4 or 8), limited so that sampleRate * factor <= 384 kHz.
    // Longest lookahead in milliseconds (the parameter's largest choice).
    static constexpr float kMaxLookaheadMs = 5.0f;
    // Lookahead delay in base-rate samples for a time in milliseconds (clamped to 0..kMaxLookaheadMs).
    static int lookaheadSamples(float ms, double sampleRate) noexcept;

    static int effectiveFactor(OSFactor requested, double sampleRate) noexcept;
    // Per-band audibility from mute/solo (bands >= numBands are never audible).
    static std::array<bool, kMaxBands> computeAudibility(const EngineSettings& settings) noexcept;

    MeterState& meters() noexcept { return meters_; }

private:
    using Oversampler = juce::dsp::Oversampling<float>;
    using Smoothed = juce::SmoothedValue<float>;

    Oversampler* activeOversampler() noexcept;
    int computeLatency(int factor, OSPhase phase) const noexcept;
    void applyConfiguration() noexcept;
    void resetCore() noexcept;
    void processChunk(juce::AudioBuffer<float>& buffer, int start, int length, int numCh) noexcept;

    EngineSettings settings_;
    MeterState meters_;
    bool nonRealtime_ = false;
    bool prepared_ = false;
    bool configured_ = false;
    bool needSnap_ = true;
    bool coreCold_ = false;

    double sampleRate_ = 44100.0;
    int maxBlock_ = 0;
    int numCh_ = 0;
    int maxFactor_ = 1;
    int factor_ = 1;
    int numBands_ = 0;
    OSPhase phase_ = OSPhase::linear;
    int latency_ = 0;
    int maxLookaheadBase_ = 0; // base-rate samples at kMaxLookaheadMs
    int lookaheadBase_ = 0;    // current lookahead, base-rate samples

    std::array<std::unique_ptr<Oversampler>, 3> fir_, iir_; // factors 2, 4, 8
    Crossover crossover_;
    std::array<BandProcessor, kMaxBands> bands_;
    AutoGain autoGain_;

    juce::AudioBuffer<float> dryProc_; // processing-rate dry copy
    std::array<juce::AudioBuffer<float>, kMaxBands> bandBuf_;
    juce::AudioBuffer<float> delayed_; // base-rate delayed input (bypass path)
    juce::AudioBuffer<float> delayLine_;
    int delayMask_ = 0, delayPos_ = 0;
    juce::AudioBuffer<float> dryDelayLine_; // processing-rate delay of the dry (lookahead)
    int dryDelayMask_ = 0, dryDelayPos_ = 0, dryDelay_ = 0;
    std::vector<float> ramp_;

    LevelMatch levelMatch_;                         // cleared by reset(), kept by resetCore()
    std::vector<const float*> matchOut_, matchRef_; // channel pointers handed to levelMatch_.analyse()

    Smoothed inGain_, mix_, outGain_, bypassW_, deltaW_;
};
} // namespace pentagrit
