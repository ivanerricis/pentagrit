// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestSignals.h"
#include "dsp/Saturator.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace pentagrit;
using namespace pentagrit::test;

namespace
{
constexpr SatType kAll[] = {SatType::tanh, SatType::softClip, SatType::tube, SatType::wavefold,
                            SatType::hardClip};

void run(Saturator& s, Signal& x)
{
    float* ch[1] = {x.data()};
    s.process(juce::dsp::AudioBlock<float>(ch, 1, x.size()));
}

double harmonicDb(const Signal& y, size_t from, size_t len, double f0, double fs, int harmonic)
{
    Signal seg(y.begin() + static_cast<std::ptrdiff_t>(from), y.begin() + static_cast<std::ptrdiff_t>(from + len));
    // Hann window to avoid leakage
    for (size_t i = 0; i < len; ++i)
        seg[i] *= 0.5f - 0.5f * static_cast<float>(std::cos(2.0 * kPi * double(i) / double(len)));
    const auto mag = fftMagnitude(seg);
    const double binHz = fs / double(nextPow2(len));
    auto peak = [&](double f)
    {
        const int c = int(std::lround(f / binHz));
        float m = 0.0f;
        for (int k = std::max(c - 3, 0); k <= std::min<int>(c + 3, int(mag.size()) - 1); ++k)
            m = std::max(m, mag[size_t(k)]);
        return double(m);
    };
    return toDb(peak(f0 * harmonic) / std::max(peak(f0), 1e-20));
}
} // namespace

TEST_CASE("Saturator curves", "[Saturator]")
{
    for (auto t : kAll)
        CHECK(Saturator::shape(t, 0.0f) == 0.0f);
    CHECK(Saturator::shape(SatType::tube, 0.0f) == 0.0f);

    SECTION("odd symmetry except tube")
    {
        for (auto t : kAll)
        {
            if (t == SatType::tube)
                continue;
            for (float u = 0.0f; u < 5.0f; u += 0.037f)
                CHECK(Saturator::shape(t, -u) == -Saturator::shape(t, u));
        }
        CHECK(Saturator::shape(SatType::tube, 0.5f) != -Saturator::shape(SatType::tube, -0.5f));
    }
    SECTION("bounded")
    {
        for (auto t : kAll)
            for (float u = -100.0f; u <= 100.0f; u += 0.05f)
            {
                const float y = Saturator::shape(t, u);
                REQUIRE(std::isfinite(y));
                REQUIRE(std::abs(y) <= 1.5f);
            }
    }
    SECTION("monotonic")
    {
        for (auto t : {SatType::tanh, SatType::softClip, SatType::hardClip})
        {
            float prev = Saturator::shape(t, -20.0f);
            for (float u = -20.0f; u <= 20.0f; u += 0.01f)
            {
                const float y = Saturator::shape(t, u);
                REQUIRE(y >= prev - 1e-6f);
                prev = y;
            }
        }
    }
    SECTION("specific values")
    {
        CHECK(std::abs(Saturator::shape(SatType::softClip, 1.0f) - Saturator::shape(SatType::softClip, 1.0001f)) < 1e-4f);
        CHECK(std::abs(Saturator::shape(SatType::softClip, -1.0f) - Saturator::shape(SatType::softClip, -1.0001f)) < 1e-4f);
        CHECK(std::abs(Saturator::shape(SatType::softClip, 1.0f) - 1.0f) < 1e-6f);
        CHECK(std::abs(Saturator::shape(SatType::wavefold, 3.0f) + 1.0f) < 1e-5f);
        CHECK(Saturator::shape(SatType::hardClip, 2.0f) == 1.0f);
    }
}

TEST_CASE("Saturator harmonics and DC", "[Saturator]")
{
    constexpr double fs = 48000.0;
    constexpr size_t n = 48000;

    auto makeSat = [&](SatType t, float drive)
    {
        Saturator s;
        s.prepare(fs, 512, 1);
        s.setDriveDb(drive);
        s.setType(t);
        // Let the type crossfade finish
        Signal warm(2000, 0.0f);
        run(s, warm);
        return s;
    };

    SECTION("tanh has no even harmonics")
    {
        auto s = makeSat(SatType::tanh, 12.0f);
        auto x = sine(n, 1000.0, fs, 0.5f);
        run(s, x);
        CHECK(harmonicDb(x, 16384, 16384, 1000.0, fs, 2) < -80.0);
    }
    SECTION("tube has audible H2")
    {
        auto s = makeSat(SatType::tube, 12.0f);
        auto x = sine(n, 1000.0, fs, 0.5f);
        run(s, x);
        CHECK(harmonicDb(x, 16384, 16384, 1000.0, fs, 2) > -40.0);
    }
    SECTION("tube DC is removed")
    {
        auto s = makeSat(SatType::tube, 12.0f);
        auto x = sine(n, 1000.0, fs, 0.5f);
        run(s, x);
        double mean = 0.0;
        for (size_t i = n - 4800; i < n; ++i)
            mean += x[i];
        mean /= 4800.0;
        CHECK(std::abs(mean) < 1e-4);
    }
}

TEST_CASE("Saturator curve switching is click free", "[Saturator]")
{
    constexpr double fs = 48000.0;
    for (auto from : {SatType::tanh, SatType::tube, SatType::hardClip, SatType::off})
        for (auto to : {SatType::wavefold, SatType::softClip, SatType::off, SatType::tanh})
        {
            if (from == to)
                continue;
            Saturator s;
            s.prepare(fs, 512, 1);
            s.setDriveDb(6.0f);
            s.setType(from);
            Signal pre(24000, 0.0f);
            run(s, pre);
            auto x = sine(9600, 100.0, fs, 0.5f);
            // settle on `from` with signal
            Signal settle = sine(24000, 100.0, fs, 0.5f);
            run(s, settle);
            s.setType(to);
            run(s, x);
            float maxJump = 0.0f;
            for (size_t i = 1; i < x.size(); ++i)
                maxJump = std::max(maxJump, std::abs(x[i] - x[i - 1]));
            INFO("from " << int(from) << " to " << int(to));
            CHECK(maxJump <= 0.05f);
        }
}

TEST_CASE("Saturator off is bit exact", "[Saturator]")
{
    Saturator s;
    s.prepare(48000.0, 512, 2);
    s.setDriveDb(36.0f);
    s.setType(SatType::off);
    auto a = noise(4096, 7);
    auto b = noise(4096, 8);
    const auto ra = a, rb = b;
    float* ch[2] = {a.data(), b.data()};
    s.process(juce::dsp::AudioBlock<float>(ch, 2, a.size()));
    CHECK(a == ra);
    CHECK(b == rb);
}

TEST_CASE("Saturator survives silence and denormals", "[Saturator]")
{
    Saturator s;
    s.prepare(96000.0, 512, 1);
    s.setType(SatType::tube);
    Signal x(96000, 1e-40f);
    run(s, x);
    for (float v : x)
        REQUIRE(std::isfinite(v));
}
