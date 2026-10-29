// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestSignals.h"
#include "dsp/Crossover.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace pentagrit;
using namespace pentagrit::test;

namespace
{
struct SplitResult
{
    std::array<Signal, kMaxBands> bands;
    Signal sum;
};

void setup(Crossover& xo, double rate, int numBands, const std::array<float, 4>& hz, int maxBlock)
{
    xo.prepare(rate, rate, maxBlock, 1);
    xo.setNumBands(numBands);
    xo.setFrequencies(hz);
    xo.reset(); // snaps the smoothed frequencies to the targets
}

SplitResult splitSignal(double rate, int numBands, const std::array<float, 4>& hz, const Signal& in)
{
    Crossover xo;
    setup(xo, rate, numBands, hz, static_cast<int>(in.size()));

    SplitResult r;
    std::array<float*, kMaxBands> ptrs{};
    Crossover::BandBlocks blocks;
    for (int b = 0; b < numBands; ++b)
    {
        auto i = static_cast<size_t>(b);
        r.bands[i].assign(in.size(), 0.0f);
        ptrs[i] = r.bands[i].data();
        blocks[i] = juce::dsp::AudioBlock<float>(&ptrs[i], 1, in.size());
    }
    const float* inPtr = in.data();
    xo.split(juce::dsp::AudioBlock<const float>(&inPtr, 1, in.size()), blocks);

    r.sum.assign(in.size(), 0.0f);
    for (int b = 0; b < numBands; ++b)
        for (size_t i = 0; i < in.size(); ++i)
            r.sum[i] += r.bands[static_cast<size_t>(b)][i];
    return r;
}

double binOf(double hz, double rate, size_t n)
{
    return hz * static_cast<double>(n) / rate;
}

double magDbAt(const std::vector<float>& mag, double hz, double rate, size_t n)
{
    const double pos = binOf(hz, rate, n);
    const auto lo = static_cast<size_t>(pos);
    const double frac = pos - static_cast<double>(lo);
    const double m = static_cast<double>(mag[lo]) * (1.0 - frac) + static_cast<double>(mag[lo + 1]) * frac;
    return toDb(m);
}

// Largest deviation (dB) of the spectrum of `s` from 0 dB between loHz and hiHz.
double maxDeviationDb(const Signal& s, double rate, double loHz, double hiHz)
{
    const auto mag = fftMagnitude(s);
    const size_t n = nextPow2(s.size());
    double worst = 0.0;
    const auto first = static_cast<size_t>(std::ceil(binOf(loHz, rate, n)));
    const auto last = static_cast<size_t>(std::floor(binOf(hiHz, rate, n)));
    for (size_t k = first; k <= last; ++k)
        worst = std::max(worst, std::abs(toDb(static_cast<double>(mag[k]))));
    return worst;
}

// Frequency exactly on an FFT bin so single-bin readings are exact.
float onBin(double hz, double rate, size_t n)
{
    return static_cast<float>(std::round(binOf(hz, rate, n)) * rate / static_cast<double>(n));
}

bool allFinite(const Signal& s)
{
    return std::all_of(s.begin(), s.end(), [](float v) { return std::isfinite(v); });
}

const std::array<float, 4> kDefaults{120.0f, 1200.0f, 3500.0f, 8000.0f};
} // namespace

TEST_CASE("Crossover band sum is flat (allpass)", "[Crossover]")
{
    constexpr double sr = 48000.0;
    constexpr size_t n = 1 << 18;

    for (int bands : {2, 3, 4, 5})
    {
        const auto r = splitSignal(sr, bands, kDefaults, impulse(n));
        const double dev = maxDeviationDb(r.sum, sr, 20.0, 20000.0);
        INFO("bands=" << bands << " max deviation dB=" << dev);
        CHECK(dev <= 0.01);
    }
}

TEST_CASE("Crossover band sum is flat at 384 kHz with f1 = 20 Hz", "[Crossover]")
{
    constexpr double sr = 384000.0;
    constexpr size_t n = 1 << 20;

    const auto r = splitSignal(sr, 5, {20.0f, 1200.0f, 3500.0f, 8000.0f}, impulse(n));
    const double dev = maxDeviationDb(r.sum, sr, 20.0, 20000.0);
    INFO("max deviation dB=" << dev);
    CHECK(dev <= 0.01);
}

TEST_CASE("Crossover band sum equals the allpass chain", "[Crossover]")
{
    constexpr double sr = 48000.0;
    const auto in = noise(1 << 15, 7);

    for (int bands : {2, 3, 4, 5})
    {
        const auto r = splitSignal(sr, bands, kDefaults, in);

        Crossover xo;
        setup(xo, sr, bands, kDefaults, static_cast<int>(in.size()));
        Signal chain = in;
        float* p = chain.data();
        xo.processAllpassChain(juce::dsp::AudioBlock<float>(&p, 1, chain.size()));

        double worst = 0.0;
        for (size_t i = 0; i < in.size(); ++i)
            worst = std::max(worst, static_cast<double>(std::abs(r.sum[i] - chain[i])));
        INFO("bands=" << bands << " max diff=" << worst);
        CHECK(worst < 1.0e-5);
    }
}

TEST_CASE("Crossover bands are -6.02 dB at their crossover frequency", "[Crossover]")
{
    constexpr double sr = 48000.0;
    constexpr size_t n = 1 << 18;

    // Well separated frequencies so the neighbouring filters do not contribute at the crossover point.
    const std::array<float, 4> hz{onBin(40.0, sr, n), onBin(200.0, sr, n), onBin(1000.0, sr, n),
                                  onBin(5000.0, sr, n)};
    const auto r = splitSignal(sr, 5, hz, impulse(n));

    for (size_t k = 0; k < 4; ++k)
    {
        const double f = static_cast<double>(hz[k]);
        const double below = magDbAt(fftMagnitude(r.bands[k]), f, sr, n);
        const double above = magDbAt(fftMagnitude(r.bands[k + 1]), f, sr, n);
        INFO("xover " << f << " Hz: lower " << below << " dB, upper " << above << " dB");
        CHECK_THAT(below, Catch::Matchers::WithinAbs(-6.02, 0.1));
        CHECK_THAT(above, Catch::Matchers::WithinAbs(-6.02, 0.1));
    }
}

TEST_CASE("Crossover bands roll off at least 22 dB/oct away from the crossover", "[Crossover]")
{
    constexpr double sr = 48000.0;
    constexpr size_t n = 1 << 18;
    const auto r = splitSignal(sr, 5, kDefaults, impulse(n));

    std::array<std::vector<float>, kMaxBands> mags;
    for (size_t b = 0; b < kMaxBands; ++b)
        mags[b] = fftMagnitude(r.bands[b]);

    // Slope between 1 and 2 octaves away from the band edge, outside the band.
    auto slope = [&](size_t band, double edge, double dir)
    {
        const double near1 = magDbAt(mags[band], edge * std::pow(2.0, dir * 1.0), sr, n);
        const double far2 = magDbAt(mags[band], edge * std::pow(2.0, dir * 2.0), sr, n);
        return far2 - near1; // dB per octave (negative)
    };

    for (size_t b = 0; b < kMaxBands; ++b)
    {
        // Two octaves above the last crossover (32 kHz) is beyond Nyquist at 48 kHz, so skip that edge.
        if (b < 4 && static_cast<double>(kDefaults[b]) * 4.0 <= 20000.0)
        {
            const double s = slope(b, static_cast<double>(kDefaults[b]), +1.0);
            INFO("band " << b << " upper slope " << s);
            CHECK(s <= -22.0);
        }
        if (b > 0)
        {
            const double s = slope(b, static_cast<double>(kDefaults[b - 1]), -1.0);
            INFO("band " << b << " lower slope " << s);
            CHECK(s <= -22.0);
        }
    }
}

TEST_CASE("Crossover enforceOrdering", "[Crossover]")
{
    const double ratio = std::cbrt(2.0);
    const double eps = 1.0e-4;

    auto checkSpacing = [&](const std::array<float, 4>& f, int bands, double baseRate)
    {
        const double fmax = std::min(20000.0, 0.45 * baseRate);
        const int active = bands - 1;
        for (int i = 0; i < active; ++i)
        {
            CHECK(f[static_cast<size_t>(i)] >= 20.0f);
            CHECK(static_cast<double>(f[static_cast<size_t>(i)]) <= fmax * (1.0 + eps));
            if (i > 0)
                CHECK(static_cast<double>(f[static_cast<size_t>(i)]) >=
                      static_cast<double>(f[static_cast<size_t>(i - 1)]) * ratio * (1.0 - eps));
        }
    };

    SECTION("already valid is untouched")
    {
        const auto f = Crossover::enforceOrdering(kDefaults, 5, 48000.0);
        for (size_t i = 0; i < 4; ++i)
            CHECK(f[i] == kDefaults[i]);
    }
    SECTION("reversed")
    {
        const auto f = Crossover::enforceOrdering({8000.0f, 3500.0f, 1200.0f, 120.0f}, 5, 48000.0);
        checkSpacing(f, 5, 48000.0);
    }
    SECTION("all equal")
    {
        const auto f = Crossover::enforceOrdering({1000.0f, 1000.0f, 1000.0f, 1000.0f}, 5, 48000.0);
        checkSpacing(f, 5, 48000.0);
        CHECK(f[0] == 1000.0f);
    }
    SECTION("above Nyquist")
    {
        const auto f = Crossover::enforceOrdering({30000.0f, 40000.0f, 50000.0f, 60000.0f}, 5, 48000.0);
        checkSpacing(f, 5, 48000.0);
        CHECK(static_cast<double>(f[3]) <= 0.45 * 48000.0 * (1.0 + eps));
    }
    SECTION("below the floor and at the ceiling")
    {
        const auto f = Crossover::enforceOrdering({1.0f, 2.0f, 3.0f, 20000.0f}, 5, 96000.0);
        checkSpacing(f, 5, 96000.0);
        CHECK(f[0] == 20.0f);
        CHECK(f[3] == 20000.0f);
    }
    SECTION("fewer bands only touch the active crossovers")
    {
        const auto f = Crossover::enforceOrdering({500.0f, 400.0f, 1.0f, 2.0f}, 3, 48000.0);
        checkSpacing(f, 3, 48000.0);
        CHECK(f[2] == 1.0f);
        CHECK(f[3] == 2.0f);
    }
    SECTION("instance exposes the effective frequencies")
    {
        Crossover xo;
        xo.prepare(48000.0, 48000.0, 512, 2);
        xo.setNumBands(5);
        xo.setFrequencies({1000.0f, 1000.0f, 1000.0f, 1000.0f});
        checkSpacing(xo.getEffectiveFrequencies(), 5, 48000.0);
    }
}

TEST_CASE("Crossover stays finite on silence, denormals and frequency sweeps", "[Crossover]")
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    Crossover xo;
    xo.prepare(sr, sr, block, 2);

    std::array<std::array<Signal, 2>, kMaxBands> out;
    std::array<std::array<float*, 2>, kMaxBands> ptrs{};
    Crossover::BandBlocks blocks;
    for (size_t b = 0; b < kMaxBands; ++b)
    {
        for (size_t c = 0; c < 2; ++c)
        {
            out[b][c].assign(block, 0.0f);
            ptrs[b][c] = out[b][c].data();
        }
        blocks[b] = juce::dsp::AudioBlock<float>(ptrs[b].data(), 2, block);
    }

    auto processBlock = [&](const Signal& s)
    {
        const std::array<const float*, 2> in{s.data(), s.data()};
        xo.split(juce::dsp::AudioBlock<const float>(in.data(), 2, block), blocks);
        for (auto& band : out)
            for (auto& ch : band)
                if (!allFinite(ch))
                    return false;
        return true;
    };

    SECTION("silence gives exact silence")
    {
        const Signal zeros(block, 0.0f);
        for (int i = 0; i < 20; ++i)
            REQUIRE(processBlock(zeros));
        for (auto& band : out)
            for (float v : band[0])
                CHECK(v == 0.0f);
    }
    SECTION("denormal input")
    {
        const Signal tiny(block, 1.0e-40f);
        for (int i = 0; i < 50; ++i)
            REQUIRE(processBlock(tiny));
    }
    SECTION("decays to zero after a burst without NaN")
    {
        REQUIRE(processBlock(noise(block, 3)));
        const Signal zeros(block, 0.0f);
        for (int i = 0; i < 2000; ++i)
            REQUIRE(processBlock(zeros));
        for (auto& band : out)
            for (float v : band[0])
                CHECK(std::abs(v) < 1.0e-20f);
    }
    SECTION("frequency changes and band count changes")
    {
        const auto in = noise(block, 5);
        for (int i = 0; i < 200; ++i)
        {
            if (i % 20 == 0)
                xo.setFrequencies({20.0f + static_cast<float>(i) * 30.0f, 1000.0f,
                                   9000.0f - static_cast<float>(i) * 20.0f, 19000.0f});
            if (i % 50 == 0)
                xo.setNumBands(2 + (i / 50) % 4);
            REQUIRE(processBlock(in));
        }
    }
}

TEST_CASE("Crossover processing rate change keeps the sum flat", "[Crossover]")
{
    constexpr double base = 48000.0;
    constexpr size_t n = 1 << 17;
    const double proc = base * 4.0;

    Crossover xo;
    xo.prepare(base, base, static_cast<int>(n), 1);
    xo.setNumBands(5);
    xo.setFrequencies(kDefaults);
    xo.setProcessingRate(proc);

    Signal in = impulse(n);
    std::array<Signal, kMaxBands> bands;
    std::array<float*, kMaxBands> ptrs{};
    Crossover::BandBlocks blocks;
    for (size_t b = 0; b < kMaxBands; ++b)
    {
        bands[b].assign(n, 0.0f);
        ptrs[b] = bands[b].data();
        blocks[b] = juce::dsp::AudioBlock<float>(&ptrs[b], 1, n);
    }
    const float* inPtr = in.data();
    xo.split(juce::dsp::AudioBlock<const float>(&inPtr, 1, n), blocks);

    Signal sum(n, 0.0f);
    for (auto& b : bands)
        for (size_t i = 0; i < n; ++i)
            sum[i] += b[i];
    CHECK(maxDeviationDb(sum, proc, 20.0, 20000.0) <= 0.01);
}
