// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "params/ParamIDs.h"
#include <array>

namespace pentagrit
{
enum class SatType
{
    off,
    tanh,
    softClip,
    tube,
    wavefold,
    hardClip
};
enum class OSFactor
{
    x1,
    x2,
    x4,
    x8
};

enum class OSPhase
{
    linear,
    minimum
};

struct BandSettings
{
    float gainDb = 0, attackDb = 0, sustainDb = 0, speed01 = .5f, driveDb = 6, mix01 = 1;
    SatType sat = SatType::off;
    bool mute = false, solo = false, bypass = false;
};

struct EngineSettings
{
    float inGainDb = 0, outGainDb = 0, mix01 = 1;
    bool autoGain = false, bypass = false;
    OSFactor os = OSFactor::x2;
    OSPhase osPhase = OSPhase::linear;
    // Offline-render oversampling: if osRenderSame, the live factor `os` is used; else `osRender`.
    OSFactor osRender = OSFactor::x1;
    bool osRenderSame = true;
    int numBands = 3;
    std::array<float, 4> xoverHz{120, 1200, 3500, 8000};
    std::array<BandSettings, kMaxBands> bands{};
};
} // namespace pentagrit
