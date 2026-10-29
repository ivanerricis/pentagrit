// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/Crossover.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <type_traits>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace pentagrit
{
namespace
{
constexpr double kButterworthK = 1.4142135623730951; // 1/Q of a 2nd order Butterworth
constexpr double kMinHz = 20.0;
constexpr double kSmoothSeconds = 0.05;
constexpr double kFlushThreshold = 1.0e-25;

// Two doubles processed together (one channel pair). SSE2 is part of the x64 baseline; other targets use a
// scalar fallback with identical arithmetic.
#if defined(_M_X64) || defined(__SSE2__)
struct D2
{
    __m128d v;
};
inline D2 operator+(D2 a, D2 b) noexcept
{
    return {_mm_add_pd(a.v, b.v)};
}
inline D2 operator-(D2 a, D2 b) noexcept
{
    return {_mm_sub_pd(a.v, b.v)};
}
inline D2 operator*(D2 a, D2 b) noexcept
{
    return {_mm_mul_pd(a.v, b.v)};
}
inline D2 splat(double x) noexcept
{
    return {_mm_set1_pd(x)};
}
inline D2 make(double lane0, double lane1) noexcept
{
    return {_mm_set_pd(lane1, lane0)};
}
inline D2 load(const double* p) noexcept
{
    return {_mm_load_pd(p)};
}
inline void store(double* p, D2 a) noexcept
{
    _mm_store_pd(p, a.v);
}
inline double lane0(D2 a) noexcept
{
    return _mm_cvtsd_f64(a.v);
}
inline double lane1(D2 a) noexcept
{
    return _mm_cvtsd_f64(_mm_unpackhi_pd(a.v, a.v));
}
#else
struct D2
{
    double a, b;
};
inline D2 operator+(D2 x, D2 y) noexcept
{
    return {x.a + y.a, x.b + y.b};
}
inline D2 operator-(D2 x, D2 y) noexcept
{
    return {x.a - y.a, x.b - y.b};
}
inline D2 operator*(D2 x, D2 y) noexcept
{
    return {x.a * y.a, x.b * y.b};
}
inline D2 splat(double x) noexcept
{
    return {x, x};
}
inline D2 make(double l0, double l1) noexcept
{
    return {l0, l1};
}
inline D2 load(const double* p) noexcept
{
    return {p[0], p[1]};
}
inline void store(double* p, D2 x) noexcept
{
    p[0] = x.a;
    p[1] = x.b;
}
inline double lane0(D2 x) noexcept
{
    return x.a;
}
inline double lane1(D2 x) noexcept
{
    return x.b;
}
#endif

struct VCoefs
{
    D2 a1, a2, a3;
};

template <typename Coefs> inline VCoefs vcoefs(const Coefs& c) noexcept
{
    return {splat(c.a1), splat(c.a2), splat(c.a3)};
}

// TPT state variable filter step (Zavalishin). Returns lowpass and bandpass; highpass = x - k*bp - lp.
template <typename State> inline void svfStep(State& st, const VCoefs& c, D2 x, D2& lp, D2& bp) noexcept
{
    const D2 kTwo = splat(2.0);
    const D2 ic1 = load(st.ic1), ic2 = load(st.ic2);
    const D2 v3 = x - ic2;
    const D2 v1 = c.a1 * ic1 + c.a2 * v3;
    const D2 v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
    store(st.ic1, kTwo * v1 - ic1);
    store(st.ic2, kTwo * v2 - ic2);
    lp = v2;
    bp = v1;
}

template <typename State> inline D2 lowpass(State& st, const VCoefs& c, D2 x) noexcept
{
    D2 lp, bp;
    svfStep(st, c, x, lp, bp);
    return lp;
}

template <typename State> inline D2 highpass(State& st, const VCoefs& c, D2 x) noexcept
{
    D2 lp, bp;
    svfStep(st, c, x, lp, bp);
    return x - splat(kButterworthK) * bp - lp;
}

// 2nd order Butterworth allpass: LP + HP - k*BP = x - 2k*BP.
template <typename State> inline D2 allpass(State& st, const VCoefs& c, D2 x) noexcept
{
    D2 lp, bp;
    svfStep(st, c, x, lp, bp);
    return x - splat(2.0 * kButterworthK) * bp;
}

template <typename State> inline void flush(State& st) noexcept
{
    for (int l = 0; l < 2; ++l)
    {
        if (std::abs(st.ic1[l]) < kFlushThreshold)
            st.ic1[l] = 0.0;
        if (std::abs(st.ic2[l]) < kFlushThreshold)
            st.ic2[l] = 0.0;
    }
}

template <typename Coefs> void computeCoefs(Coefs& c, double logHz, double rate) noexcept
{
    const double hz = std::min(std::exp(logHz), 0.49 * rate);
    const double g = std::tan(std::numbers::pi * hz / rate);
    c.a1 = 1.0 / (1.0 + g * (g + kButterworthK));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
}
} // namespace

void Crossover::prepare(double baseRate, double procRate, int, int numCh)
{
    baseRate_ = baseRate;
    procRate_ = procRate;
    channels_.assign(static_cast<size_t>((std::max(numCh, 1) + 1) / 2), ChannelState{});
    effective_ = enforceOrdering(requested_, numBands_, baseRate_);
    updateTargets();
    reset();
}

void Crossover::setProcessingRate(double procRate) noexcept
{
    procRate_ = procRate;
    updateTargets();
    reset();
}

void Crossover::reset() noexcept
{
    for (auto& ch : channels_)
        ch = ChannelState{};
    snap(splitSmoother_);
    snap(drySmoother_);
}

void Crossover::setNumBands(int numBands) noexcept
{
    const int n = std::clamp(numBands, 2, kMaxBands);
    const bool changed = n != numBands_;
    numBands_ = n;
    effective_ = enforceOrdering(requested_, numBands_, baseRate_);
    updateTargets();
    if (changed)
        reset();
}

void Crossover::setFrequencies(const std::array<float, 4>& hz) noexcept
{
    requested_ = hz;
    effective_ = enforceOrdering(requested_, numBands_, baseRate_);
    updateTargets();
}

std::array<float, 4> Crossover::getEffectiveFrequencies() const noexcept
{
    return effective_;
}

std::array<float, 4> Crossover::enforceOrdering(std::array<float, 4> hz, int numBands,
                                                double baseRate) noexcept
{
    const double ratio = std::cbrt(2.0);
    const double maxHz = std::max(kMinHz, std::min(20000.0, 0.45 * baseRate));
    const int active = std::clamp(numBands, 2, kMaxBands) - 1;

    std::array<double, 4> f{};
    for (int i = 0; i < active; ++i)
    {
        const double v = static_cast<double>(hz[static_cast<size_t>(i)]);
        f[static_cast<size_t>(i)] = std::isfinite(v) ? std::clamp(v, kMinHz, maxHz) : kMinHz;
    }

    // Forward pass pushes up, backward pass pulls down from the ceiling, final forward pass restores the
    // floor.
    for (int i = 1; i < active; ++i)
        f[static_cast<size_t>(i)] =
            std::max(f[static_cast<size_t>(i)], f[static_cast<size_t>(i - 1)] * ratio);
    f[static_cast<size_t>(active - 1)] = std::min(f[static_cast<size_t>(active - 1)], maxHz);
    for (int i = active - 2; i >= 0; --i)
        f[static_cast<size_t>(i)] =
            std::min(f[static_cast<size_t>(i)], f[static_cast<size_t>(i + 1)] / ratio);
    f[0] = std::max(f[0], kMinHz);
    for (int i = 1; i < active; ++i)
        f[static_cast<size_t>(i)] =
            std::max(f[static_cast<size_t>(i)], f[static_cast<size_t>(i - 1)] * ratio);

    for (int i = 0; i < active; ++i)
        hz[static_cast<size_t>(i)] = static_cast<float>(f[static_cast<size_t>(i)]);
    return hz;
}

void Crossover::updateTargets() noexcept
{
    for (size_t i = 0; i < 4; ++i)
        targetLogHz_[i] = std::log(std::max(static_cast<double>(effective_[i]), kMinHz));
    smoothAlpha_ =
        1.0 - std::exp(-static_cast<double>(kCoefInterval) / (kSmoothSeconds * std::max(procRate_, 1.0)));
}

void Crossover::snap(Smoother& s) const noexcept
{
    for (size_t i = 0; i < 4; ++i)
    {
        s.logHz[i] = targetLogHz_[i];
        computeCoefs(s.coefs[i], s.logHz[i], procRate_);
    }
    s.counter = 0;
}

void Crossover::advance(Smoother& s) const noexcept
{
    for (size_t i = 0; i < 4; ++i)
    {
        const double diff = targetLogHz_[i] - s.logHz[i];
        if (diff == 0.0)
            continue;
        s.logHz[i] = std::abs(diff) < 1.0e-9 ? targetLogHz_[i] : s.logHz[i] + diff * smoothAlpha_;
        computeCoefs(s.coefs[i], s.logHz[i], procRate_);
    }
}

template <typename Fn> void Crossover::forEachChunk(Smoother& s, int numSamples, Fn&& fn) noexcept
{
    int pos = 0;
    while (pos < numSamples)
    {
        const int len = std::min(numSamples - pos, kCoefInterval - s.counter);
        fn(pos, len);
        pos += len;
        s.counter += len;
        if (s.counter >= kCoefInterval)
        {
            s.counter = 0;
            advance(s);
        }
    }
}

void Crossover::split(const juce::dsp::AudioBlock<const float>& in, BandBlocks& bands) noexcept
{
    const int numSamples = static_cast<int>(in.getNumSamples());
    const int numCh = std::min(static_cast<int>(in.getNumChannels()), static_cast<int>(channels_.size()) * 2);
    const int stages = numBands_ - 1;

    // One call handles a channel pair (lanes); a missing second channel is fed zeros and its output dropped.
    auto run = [&](int ch0, int start, int len)
    {
        const bool two = ch0 + 1 < numCh;
        const auto& coefs = splitSmoother_.coefs;
        VCoefs vc[4];
        for (int s = 0; s < stages; ++s)
            vc[s] = vcoefs(coefs[static_cast<size_t>(s)]);
        ChannelState& st = channels_[static_cast<size_t>(ch0 / 2)];
        const float* src0 = in.getChannelPointer(static_cast<size_t>(ch0));
        const float* src1 = two ? in.getChannelPointer(static_cast<size_t>(ch0 + 1)) : src0;
        float* out0[kMaxBands];
        float* out1[kMaxBands];
        for (int b = 0; b < numBands_; ++b)
        {
            out0[b] = bands[static_cast<size_t>(b)].getChannelPointer(static_cast<size_t>(ch0));
            out1[b] =
                two ? bands[static_cast<size_t>(b)].getChannelPointer(static_cast<size_t>(ch0 + 1)) : nullptr;
        }

        for (int i = start; i < start + len; ++i)
        {
            D2 x = make(static_cast<double>(src0[i]), two ? static_cast<double>(src1[i]) : 0.0);
            for (int s = 0; s < stages; ++s)
            {
                const auto si = static_cast<size_t>(s);
                D2 lp, bp;
                svfStep(st.lp1[si], vc[s], x, lp, bp);
                const D2 hp1 = x - splat(kButterworthK) * bp - lp;
                D2 low = lowpass(st.lp2[si], vc[s], lp);
                const D2 high = highpass(st.hp2[si], vc[s], hp1);
                // Phase-match this band to the later splits.
                for (int t = s + 1; t < stages; ++t)
                    low = allpass(st.comp[si][static_cast<size_t>(t)], vc[t], low);
                out0[s][i] = static_cast<float>(lane0(low));
                if (two)
                    out1[s][i] = static_cast<float>(lane1(low));
                x = high;
            }
            out0[stages][i] = static_cast<float>(lane0(x));
            if (two)
                out1[stages][i] = static_cast<float>(lane1(x));
        }
    };

    forEachChunk(splitSmoother_, numSamples,
                 [&](int start, int len)
                 {
                     for (int ch = 0; ch < numCh; ch += 2)
                         run(ch, start, len);
                 });

    for (auto& st : channels_)
    {
        for (auto& f : st.lp1)
            flush(f);
        for (auto& f : st.lp2)
            flush(f);
        for (auto& f : st.hp2)
            flush(f);
        for (auto& a : st.comp)
            for (auto& f : a)
                flush(f);
    }
}

void Crossover::processAllpassChain(const juce::dsp::AudioBlock<float>& io) noexcept
{
    const int numSamples = static_cast<int>(io.getNumSamples());
    const int numCh = std::min(static_cast<int>(io.getNumChannels()), static_cast<int>(channels_.size()) * 2);
    const int stages = numBands_ - 1;

    auto run = [&](int ch0, int start, int len)
    {
        const bool two = ch0 + 1 < numCh;
        VCoefs vc[4];
        for (int s = 0; s < stages; ++s)
            vc[s] = vcoefs(drySmoother_.coefs[static_cast<size_t>(s)]);
        ChannelState& st = channels_[static_cast<size_t>(ch0 / 2)];
        float* d0 = io.getChannelPointer(static_cast<size_t>(ch0));
        float* d1 = two ? io.getChannelPointer(static_cast<size_t>(ch0 + 1)) : d0;
        for (int i = start; i < start + len; ++i)
        {
            D2 x = make(static_cast<double>(d0[i]), two ? static_cast<double>(d1[i]) : 0.0);
            for (int s = 0; s < stages; ++s)
                x = allpass(st.dry[static_cast<size_t>(s)], vc[s], x);
            d0[i] = static_cast<float>(lane0(x));
            if (two)
                d1[i] = static_cast<float>(lane1(x));
        }
    };

    forEachChunk(drySmoother_, numSamples,
                 [&](int start, int len)
                 {
                     for (int ch = 0; ch < numCh; ch += 2)
                         run(ch, start, len);
                 });

    for (auto& st : channels_)
        for (auto& f : st.dry)
            flush(f);
}
} // namespace pentagrit
