// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace pentagrit
{
inline constexpr float kSilenceDb = -100.0f;

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }

inline float gainToDb(float gain) noexcept
{
    return gain > 1.0e-5f ? 20.0f * std::log10(gain) : kSilenceDb;
}

// One-pole smoothing coefficient for a time constant in seconds at the given rate.
inline float timeToCoeff(double seconds, double sampleRate) noexcept
{
    if (seconds <= 0.0 || sampleRate <= 0.0)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (seconds * sampleRate)));
}

template <typename T>
constexpr T clampTo(T v, T lo, T hi) noexcept
{
    return std::min(std::max(v, lo), hi);
}
} // namespace pentagrit
