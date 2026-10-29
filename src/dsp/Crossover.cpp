// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/Crossover.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <type_traits>

namespace pentagrit
{
namespace
{
constexpr double kButterworthK = 1.4142135623730951; // 1/Q of a 2nd order Butterworth
constexpr double kMinHz = 20.0;
constexpr double kSmoothSeconds = 0.05;
constexpr double kFlushThreshold = 1.0e-25;

// TPT state variable filter step (Zavalishin). Returns lowpass and bandpass; highpass = x - k*bp - lp.
template <typename State, typename Coefs>
inline void svfStep(State& st, const Coefs& c, double x, double& lp, double& bp) noexcept
{
    const double v3 = x - st.ic2;
    const double v1 = c.a1 * st.ic1 + c.a2 * v3;
    const double v2 = st.ic2 + c.a2 * st.ic1 + c.a3 * v3;
    st.ic1 = 2.0 * v1 - st.ic1;
    st.ic2 = 2.0 * v2 - st.ic2;
    lp = v2;
    bp = v1;
}

template <typename State, typename Coefs> inline double lowpass(State& st, const Coefs& c, double x) noexcept
{
    double lp, bp;
    svfStep(st, c, x, lp, bp);
    return lp;
}

template <typename State, typename Coefs> inline double highpass(State& st, const Coefs& c, double x) noexcept
{
    double lp, bp;
    svfStep(st, c, x, lp, bp);
    return x - kButterworthK * bp - lp;
}

// 2nd order Butterworth allpass: LP + HP - k*BP = x - 2k*BP.
template <typename State, typename Coefs> inline double allpass(State& st, const Coefs& c, double x) noexcept
{
    double lp, bp;
    svfStep(st, c, x, lp, bp);
    return x - 2.0 * kButterworthK * bp;
}

template <typename State> inline void flush(State& st) noexcept
{
    if (std::abs(st.ic1) < kFlushThreshold)
        st.ic1 = 0.0;
    if (std::abs(st.ic2) < kFlushThreshold)
        st.ic2 = 0.0;
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
    channels_.assign(static_cast<size_t>(std::max(numCh, 1)), ChannelState{});
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
    const int numCh = std::min(static_cast<int>(in.getNumChannels()), static_cast<int>(channels_.size()));
    const int stages = numBands_ - 1;

    // Processes NC channels starting at ch0 in lock-step so their independent dependency chains overlap.
    auto run = [&](auto tag, int ch0, int start, int len)
    {
        constexpr int NC = decltype(tag)::value;
        const auto& coefs = splitSmoother_.coefs;
        ChannelState* st[NC];
        const float* src[NC];
        float* out[NC][kMaxBands];
        for (int c = 0; c < NC; ++c)
        {
            st[c] = &channels_[static_cast<size_t>(ch0 + c)];
            src[c] = in.getChannelPointer(static_cast<size_t>(ch0 + c));
            for (int b = 0; b < numBands_; ++b)
                out[c][b] = bands[static_cast<size_t>(b)].getChannelPointer(static_cast<size_t>(ch0 + c));
        }

        for (int i = start; i < start + len; ++i)
        {
            double x[NC];
            for (int c = 0; c < NC; ++c)
                x[c] = static_cast<double>(src[c][i]);
            for (int s = 0; s < stages; ++s)
            {
                const auto si = static_cast<size_t>(s);
                const auto& cf = coefs[si];
                double low[NC], high[NC];
                for (int c = 0; c < NC; ++c)
                {
                    double lp, bp;
                    svfStep(st[c]->lp1[si], cf, x[c], lp, bp);
                    const double hp1 = x[c] - kButterworthK * bp - lp;
                    low[c] = lowpass(st[c]->lp2[si], cf, lp);
                    high[c] = highpass(st[c]->hp2[si], cf, hp1);
                }
                // Phase-match this band to the later splits.
                for (int t = s + 1; t < stages; ++t)
                    for (int c = 0; c < NC; ++c)
                        low[c] = allpass(st[c]->comp[si][static_cast<size_t>(t)],
                                         coefs[static_cast<size_t>(t)], low[c]);
                for (int c = 0; c < NC; ++c)
                {
                    out[c][s][i] = static_cast<float>(low[c]);
                    x[c] = high[c];
                }
            }
            for (int c = 0; c < NC; ++c)
                out[c][stages][i] = static_cast<float>(x[c]);
        }
    };

    forEachChunk(splitSmoother_, numSamples,
                 [&](int start, int len)
                 {
                     int ch = 0;
                     for (; ch + 1 < numCh; ch += 2)
                         run(std::integral_constant<int, 2>{}, ch, start, len);
                     if (ch < numCh)
                         run(std::integral_constant<int, 1>{}, ch, start, len);
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
    const int numCh = std::min(static_cast<int>(io.getNumChannels()), static_cast<int>(channels_.size()));
    const int stages = numBands_ - 1;

    auto run = [&](auto tag, int ch0, int start, int len)
    {
        constexpr int NC = decltype(tag)::value;
        const auto& coefs = drySmoother_.coefs;
        ChannelState* st[NC];
        float* data[NC];
        for (int c = 0; c < NC; ++c)
        {
            st[c] = &channels_[static_cast<size_t>(ch0 + c)];
            data[c] = io.getChannelPointer(static_cast<size_t>(ch0 + c));
        }
        for (int i = start; i < start + len; ++i)
        {
            double x[NC];
            for (int c = 0; c < NC; ++c)
                x[c] = static_cast<double>(data[c][i]);
            for (int s = 0; s < stages; ++s)
                for (int c = 0; c < NC; ++c)
                    x[c] = allpass(st[c]->dry[static_cast<size_t>(s)], coefs[static_cast<size_t>(s)], x[c]);
            for (int c = 0; c < NC; ++c)
                data[c][i] = static_cast<float>(x[c]);
        }
    };

    forEachChunk(drySmoother_, numSamples,
                 [&](int start, int len)
                 {
                     int ch = 0;
                     for (; ch + 1 < numCh; ch += 2)
                         run(std::integral_constant<int, 2>{}, ch, start, len);
                     if (ch < numCh)
                         run(std::integral_constant<int, 1>{}, ch, start, len);
                 });

    for (auto& st : channels_)
        for (auto& f : st.dry)
            flush(f);
}
} // namespace pentagrit
