// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include "params/ParamIDs.h"
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

namespace pentagrit
{
// Lookahead choices (pid::lookahead): Off, 1 ms, 2 ms, 5 ms.
inline constexpr std::array<float, 4> kLookaheadMs{0.0f, 1.0f, 2.0f, 5.0f};

inline constexpr int kNumParameters = 15 + kMaxBands * kNumBandParams; // 65

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Cached raw (denormalised) atomics, safe to read from the audio thread.
struct ParamRefs
{
    explicit ParamRefs(juce::AudioProcessorValueTreeState& apvts);

    std::atomic<float>*inGain, *outGain, *mix, *autoGain, *oversampling, *numBands, *bypass, *osPhase,
        *osRender, *driveComp, *lookahead;
    std::array<std::atomic<float>*, 4> xover;

    struct Band
    {
        std::atomic<float>*gain, *attack, *sustain, *speed, *satType, *drive, *mix, *mute, *solo, *bypass;
    };
    std::array<Band, kMaxBands> bands;

    EngineSettings snapshot() const noexcept;
};
} // namespace pentagrit
