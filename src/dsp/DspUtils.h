// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace pentagrit
{
inline constexpr float kSilenceDb = -100.0f;

// 2^x for x in about [-120, 120] (clamped). Range reduction to [-0.5, 0.5] and a degree-5 Taylor polynomial of
// e^(f ln2); relative error < 3e-6 (< 3e-5 dB). Branch free, so loops using it auto-vectorize on baseline SSE2.
inline float fastExp2(float x) noexcept
{
    x = std::min(std::max(x, -120.0f), 120.0f);
    const int i = static_cast<int>(x + (x >= 0.0f ? 0.5f : -0.5f)); // round to nearest
    const float f = x - static_cast<float>(i);
    float p = 1.54035304e-4f * f + 1.33335581e-3f;
    p = p * f + 9.61812911e-3f;
    p = p * f + 5.55041087e-2f;
    p = p * f + 2.40226507e-1f;
    p = p * f + 6.93147181e-1f;
    p = p * f + 1.0f;
    return p * std::bit_cast<float>(static_cast<std::uint32_t>(i + 127) << 23);
}

// log2(x) for normal positive x. Mantissa in [sqrt(.5), sqrt(2)), atanh series; abs error < 2e-7 (< 2e-6 dB).
inline float fastLog2(float x) noexcept
{
    auto bits = std::bit_cast<std::uint32_t>(x);
    const bool hi = (bits & 0x007fffffu) > 0x3504f3u; // mantissa above sqrt(2)-1: use the next octave
    bits = (bits & 0x007fffffu) | (hi ? 0x3f000000u : 0x3f800000u);
    const int e = static_cast<int>(std::bit_cast<std::uint32_t>(x) >> 23) - 127 + (hi ? 1 : 0);
    const float m = std::bit_cast<float>(bits);
    const float s = (m - 1.0f) / (m + 1.0f);
    const float s2 = s * s;
    const float p = s * (2.88539008f + s2 * (0.961796694f + s2 * (0.577078016f + s2 * 0.412198595f)));
    return static_cast<float>(e) + p;
}

inline float dbToGain(float db) noexcept
{
    return db > -300.0f ? fastExp2(db * 0.166096405f) : 0.0f; // log2(10) / 20
}

inline float gainToDb(float gain) noexcept
{
    return gain > 1.0e-5f ? 6.02059991f * fastLog2(gain) : kSilenceDb; // 20 log10(2) * log2(g)
}

// tanh(x): [7/6] Pade approximant (continued fraction), input clamped to +-5 and output to +-1. Odd, exactly 0 at
// 0, relative error ~1e-12 near 0, max absolute error 9.7e-5 (reached around |x| = 4.97 where tanh is 0.9999).
inline float fastTanh(float x) noexcept
{
    x = std::min(std::max(x, -5.0f), 5.0f);
    const float x2 = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2));
    return std::min(std::max(num / den, -1.0f), 1.0f);
}

// sin(pi/2 * u) for any finite u (NaN/Inf map to 0): range reduction to the period [-2, 2], fold to [-1, 1], odd
// polynomial (Taylor, degree 11). Max absolute error < 1e-7.
inline float fastSinHalfPi(float u) noexcept
{
    u = (u > -1.0e5f && u < 1.0e5f) ? u : 0.0f;
    const float t = u * 0.25f;
    const int r = static_cast<int>(t + (t >= 0.0f ? 0.5f : -0.5f));
    float w = 4.0f * (t - static_cast<float>(r)); // [-2, 2]
    w = w > 1.0f ? 2.0f - w : w;
    w = w < -1.0f ? -2.0f - w : w;
    const float w2 = w * w;
    float p = 5.692172922e-08f * w2 - 3.598843235e-06f;
    p = p * w2 + 1.604411848e-04f;
    p = p * w2 - 4.681754135e-03f;
    p = p * w2 + 7.969262625e-02f;
    p = p * w2 - 6.459640975e-01f;
    p = p * w2 + 1.570796327f;
    return w * p;
}

// One-pole smoothing coefficient for a time constant in seconds at the given rate.
inline float timeToCoeff(double seconds, double sampleRate) noexcept
{
    if (seconds <= 0.0 || sampleRate <= 0.0)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (seconds * sampleRate)));
}

template <typename T> constexpr T clampTo(T v, T lo, T hi) noexcept
{
    return std::min(std::max(v, lo), hi);
}
} // namespace pentagrit
