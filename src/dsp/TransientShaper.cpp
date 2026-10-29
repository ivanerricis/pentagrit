// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/TransientShaper.h"

#include "dsp/DspUtils.h"

#include <bit>
#include <cstdint>

namespace pentagrit
{
namespace
{
constexpr float kFloor = 1.0e-9f;       // follower floor (-180 dBFS), keeps everything normal
constexpr float kDbPerLog2 = 6.0205999f; // 20*log10(2)
constexpr float kAttackDeadbandDb = 2.0f;
constexpr float kGateLin = 3.1622777e-4f; // -70 dBFS

// Fast log2 approximation (polynomial on the mantissa), error < ~0.01 dB-equivalent.
inline float fastLog2(float x) noexcept
{
    const auto bits = std::bit_cast<std::uint32_t>(x);
    const float e = static_cast<float>(static_cast<int>((bits >> 23) & 0xff) - 127);
    const auto mb = (bits & 0x007fffffu) | 0x3f800000u;
    const float m = std::bit_cast<float>(mb); // [1,2)
    const float t = m - 1.0f;
    // minimax-style cubic for log2(1+t)
    const float p = t * (1.4425449f + t * (-0.7181452f + t * (0.2780930f - t * 0.0640930f)));
    return e + p;
}

inline float toDb(float x) noexcept { return kDbPerLog2 * fastLog2(std::max(x, kFloor)); }
} // namespace

void TransientShaper::prepare(double procRate, int, int)
{
    setProcessingRate(procRate);
    reset();
}

void TransientShaper::setProcessingRate(double procRate) noexcept
{
    rate_ = procRate > 0.0 ? procRate : 44100.0;
    updateCoeffs();
}

void TransientShaper::reset() noexcept
{
    e1_ = e2_ = e3_ = e4_ = 0.0f;
    gSmooth_ = 0.0f;
    lastGainDb_ = 0.0f;
}

void TransientShaper::updateCoeffs() noexcept
{
    const double k = std::pow(4.0, 1.0 - 2.0 * static_cast<double>(speed01_));
    const double fLow = lowHintHz_ > 0.0f ? static_cast<double>(lowHintHz_) : 30.0;
    const double tmin = std::min(0.1, 2.0 / fLow);
    const double r1 = std::max(0.060 * k, tmin);
    const double r3 = std::max(0.020 * k, tmin);
    const double r4 = std::max(0.250 * k, 4.0 * tmin);
    aFast_ = timeToCoeff(0.0002 * k, rate_);
    aE2_ = timeToCoeff(0.015 * k, rate_);
    rE1_ = timeToCoeff(r1, rate_);
    rE3_ = timeToCoeff(r3, rate_);
    rE4_ = timeToCoeff(r4, rate_);
    aGain_ = timeToCoeff(0.001, rate_);
}

void TransientShaper::setParameters(float attackDb, float sustainDb, float speed01) noexcept
{
    attackDb_ = attackDb;
    sustainDb_ = sustainDb;
    const float s = clampTo(speed01, 0.0f, 1.0f);
    if (s != speed01_)
    {
        speed01_ = s;
        updateCoeffs();
    }
}

void TransientShaper::setLowFrequencyHint(float hz) noexcept
{
    if (hz != lowHintHz_)
    {
        lowHintHz_ = hz;
        updateCoeffs();
    }
}

void TransientShaper::process(const juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numCh = block.getNumChannels();
    const auto n = block.getNumSamples();
    if (numCh == 0 || n == 0)
        return;

    float* ch[8];
    const size_t nc = std::min<size_t>(numCh, 8);
    for (size_t c = 0; c < nc; ++c)
        ch[c] = block.getChannelPointer(c);

    float e1 = e1_, e2 = e2_, e3 = e3_, e4 = e4_, g = gSmooth_;
    const float aF = aFast_, aS = aE2_, r1 = rE1_, r3 = rE3_, r4 = rE4_, ag = aGain_;
    const float atk = attackDb_, sus = sustainDb_;

    auto follow = [](float e, float x, float a, float r) noexcept {
        const float c = x > e ? a : r;
        return c * e + (1.0f - c) * x;
    };

    for (size_t i = 0; i < n; ++i)
    {
        float x = 0.0f;
        for (size_t c = 0; c < nc; ++c)
        {
            const float v = std::abs(ch[c][i]);
            x = v > x ? v : x;
        }
        if (!(x < 1.0e6f)) // NaN/Inf guard
            x = 0.0f;

        e1 = follow(e1, x, aF, r1);
        e2 = follow(e2, x, aS, r1);
        e3 = follow(e3, x, aF, r3);
        e4 = follow(e4, x, aF, r4);
        // flush denormals
        e1 = e1 < 1.0e-20f ? 0.0f : e1;
        e2 = e2 < 1.0e-20f ? 0.0f : e2;
        e3 = e3 < 1.0e-20f ? 0.0f : e3;
        e4 = e4 < 1.0e-20f ? 0.0f : e4;

        float gT = 0.0f;
        if (e4 >= kGateLin)
        {
            // 2 dB dead band: the slow follower of a steady rectified sine sits ~1.7 dB below the fast one
            const float dA = std::max(0.0f, toDb(e1) - toDb(e2) - kAttackDeadbandDb);
            const float dS = std::max(0.0f, toDb(e4) - toDb(e3));
            const float a = std::min(1.0f, dA * (1.0f / 12.0f));
            const float s = std::min(1.0f, dS * (1.0f / 12.0f));
            gT = clampTo(atk * a + sus * s, -36.0f, 24.0f);
        }
        g = ag * g + (1.0f - ag) * gT;
        g = std::abs(g) < 1.0e-12f ? 0.0f : g;

        const float lin = dbToGain(g);
        for (size_t c = 0; c < nc; ++c)
            ch[c][i] *= lin;
    }

    e1_ = e1;
    e2_ = e2;
    e3_ = e3;
    e4_ = e4;
    gSmooth_ = g;
    lastGainDb_ = g;
}

float TransientShaper::getLastGainDb() const noexcept { return lastGainDb_; }
} // namespace pentagrit
