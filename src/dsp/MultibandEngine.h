// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include "dsp/Meters.h"
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

namespace pentagrit
{
// Full signal chain (oversampling, crossover, bands, autogain, dry/wet, bypass). Owned by WP4.
class MultibandEngine
{
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setSettings(const EngineSettings& settings) noexcept;

    // Processes in place; large buffers are chunked internally.
    void process(juce::AudioBuffer<float>& buffer) noexcept;

    int getLatencySamples() const noexcept;

    // Oversampling factor actually used (1, 2, 4 or 8), limited so that sampleRate * factor <= 384 kHz.
    static int effectiveFactor(OSFactor requested, double sampleRate) noexcept;
    // Per-band audibility from mute/solo (bands >= numBands are never audible).
    static std::array<bool, kMaxBands> computeAudibility(const EngineSettings& settings) noexcept;

    MeterState& meters() noexcept { return meters_; }

private:
    EngineSettings settings_;
    MeterState meters_;
    int latency_ = 0;
};
} // namespace pentagrit
