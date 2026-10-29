// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <juce_core/juce_core.h>

// Parameter IDs are STABLE: never rename them (CLAP hashes them, hosts store them).
namespace pentagrit
{
inline constexpr int kMaxBands = 5;
inline constexpr int kNumXovers = 4;
inline constexpr int kParamVersion = 1;
inline constexpr int kStateVersion = 1;
inline constexpr const char* kStateTag = "PENTAGRIT_STATE";

namespace pid
{
inline constexpr const char *inGain = "inGain", *outGain = "outGain", *mix = "mix", *autoGain = "autoGain",
                            *oversampling = "oversampling", *numBands = "numBands", *bypass = "bypass",
                            *osPhase = "osPhase", *osRender = "osRender";
inline constexpr std::array<const char*, 4> xover{"xover1", "xover2", "xover3", "xover4"};
} // namespace pid

enum class BandParam
{
    gain,
    attack,
    sustain,
    speed,
    satType,
    drive,
    mix,
    mute,
    solo,
    bypass
};
inline constexpr int kNumBandParams = 10;

// (0, gain) -> "b1_gain"
juce::String bandParamID(int band0, BandParam p);
} // namespace pentagrit
