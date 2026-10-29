// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include "params/ParamIDs.h"
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

namespace pentagrit
{
inline constexpr int kNumParameters = 13 + kMaxBands * kNumBandParams; // 63

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Cached raw (denormalised) atomics, safe to read from the audio thread.
struct ParamRefs
{
    explicit ParamRefs(juce::AudioProcessorValueTreeState& apvts);

    std::atomic<float>*inGain, *outGain, *mix, *autoGain, *oversampling, *numBands, *bypass, *osPhase,
        *osRender;
    std::array<std::atomic<float>*, 4> xover;

    struct Band
    {
        std::atomic<float>*gain, *attack, *sustain, *speed, *satType, *drive, *mix, *mute, *solo, *bypass;
    };
    std::array<Band, kMaxBands> bands;

    EngineSettings snapshot() const noexcept;
};
} // namespace pentagrit
