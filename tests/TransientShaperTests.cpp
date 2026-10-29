// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/TransientShaper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Run
{
    std::vector<float> in, out, gain;
    double fs = 48000.0;
};

// 1 kHz sine: 0.2 s at 0.05, then at t0 steps to 0.5 and decays with tau = 100 ms.
std::vector<float> makeBurst(double fs, double total)
{
    const double t0 = 0.2;
    std::vector<float> x(static_cast<size_t>(total * fs));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double>(i) / fs;
        const double env = t < t0 ? 0.05 : 0.5 * std::exp(-(t - t0) / 0.1);
        x[i] = static_cast<float>(env * std::sin(2.0 * kPi * 1000.0 * t));
    }
    return x;
}

Run runShaper(const std::vector<float>& in, double fs, float atk, float sus, float speed,
              int block = 256)
{
    pentagrit::TransientShaper ts;
    ts.prepare(fs, block, 1);
    ts.setParameters(atk, sus, speed);
    Run r;
    r.fs = fs;
    r.in = in;
    r.out = in;
    r.gain.resize(in.size());
    for (size_t pos = 0; pos < in.size(); pos += static_cast<size_t>(block))
    {
        const auto n = std::min<size_t>(static_cast<size_t>(block), in.size() - pos);
        float* ptr = r.out.data() + pos;
        juce::dsp::AudioBlock<float> b(&ptr, 1, n);
        // per-sample gain trace: process in 1-sample slices is too slow; use block end value
        ts.process(b);
        for (size_t i = 0; i < n; ++i)
            r.gain[pos + i] = ts.getLastGainDb();
    }
    return r;
}

// Per-sample gain trace (block of 1) for precise timing checks.
Run runShaperPerSample(const std::vector<float>& in, double fs, float atk, float sus, float speed,
                       float hintHz = 200.0f)
{
    pentagrit::TransientShaper ts;
    ts.prepare(fs, 1, 1);
    ts.setLowFrequencyHint(hintHz);
    ts.setParameters(atk, sus, speed);
    Run r;
    r.fs = fs;
    r.in = in;
    r.out = in;
    r.gain.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        float* ptr = r.out.data() + i;
        juce::dsp::AudioBlock<float> b(&ptr, 1, 1);
        ts.process(b);
        r.gain[i] = ts.getLastGainDb();
    }
    return r;
}

size_t idx(const Run& r, double t) { return static_cast<size_t>(t * r.fs); }

double peakDb(const std::vector<float>& v, size_t a, size_t b)
{
    float p = 0;
    for (size_t i = a; i < b; ++i)
        p = std::max(p, std::abs(v[i]));
    return 20.0 * std::log10(std::max(p, 1e-12f));
}

double rmsDb(const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0;
    for (size_t i = a; i < b; ++i)
        s += static_cast<double>(v[i]) * v[i];
    return 10.0 * std::log10(std::max(s / static_cast<double>(b - a), 1e-24));
}

constexpr double kT0 = 0.2;
} // namespace

TEST_CASE("Attack boost raises the transient peak and returns to unity", "[transient]")
{
    const auto in = makeBurst(48000.0, 0.6);
    const auto r = runShaperPerSample(in, 48000.0, 12.0f, 0.0f, 0.5f);
    const auto a = idx(r, kT0), b = idx(r, kT0 + 0.005);
    CHECK(peakDb(r.out, a, b) - peakDb(r.in, a, b) >= 6.0);
    for (size_t i = idx(r, kT0 + 0.06); i < idx(r, kT0 + 0.15); ++i)
        REQUIRE(std::abs(r.gain[i]) <= 1.0f);
}

TEST_CASE("Sustain boost lifts the tail and leaves the onset", "[transient]")
{
    const auto in = makeBurst(48000.0, 0.6);
    const auto r = runShaperPerSample(in, 48000.0, 0.0f, 12.0f, 0.5f);
    const auto a = idx(r, kT0 + 0.05), b = idx(r, kT0 + 0.3);
    CHECK(rmsDb(r.out, a, b) - rmsDb(r.in, a, b) >= 6.0);
    const auto a0 = idx(r, kT0), b0 = idx(r, kT0 + 0.002);
    CHECK(std::abs(rmsDb(r.out, a0, b0) - rmsDb(r.in, a0, b0)) <= 2.0);
}

TEST_CASE("Steady sine settles to unity gain", "[transient]")
{
    std::vector<float> in(48000);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / 48000.0));
    const auto r = runShaperPerSample(in, 48000.0, 12.0f, 12.0f, 0.5f);
    for (size_t i = idx(r, 0.5); i < in.size(); ++i)
        REQUIRE(std::abs(r.gain[i]) <= 0.5f);
}

TEST_CASE("Faster speed shortens the boosted span", "[transient]")
{
    const auto in = makeBurst(48000.0, 0.6);
    auto span = [&](float speed) {
        const auto r = runShaperPerSample(in, 48000.0, 12.0f, 0.0f, speed);
        size_t n = 0;
        for (size_t i = idx(r, kT0); i < idx(r, kT0 + 0.3); ++i)
            n += r.gain[i] > 3.0f ? 1 : 0;
        return n;
    };
    const auto slow = span(0.1f), mid = span(0.5f), fast = span(0.9f);
    CHECK(fast < mid);
    CHECK(mid < slow);
}

TEST_CASE("Rate independence: 4x processing rate matches 1x", "[transient]")
{
    const auto in1 = makeBurst(48000.0, 0.6);
    const auto in4 = makeBurst(192000.0, 0.6);
    for (float atk : {12.0f, -12.0f})
    {
        const auto r1 = runShaperPerSample(in1, 48000.0, atk, 6.0f, 0.5f);
        const auto r4 = runShaperPerSample(in4, 192000.0, atk, 6.0f, 0.5f);
        for (double t = 0.19; t < 0.55; t += 0.01)
        {
            // compare the smoothed gain (max over 1 ms window)
            auto win = [&](const Run& r) {
                float m = -100, lo = 100;
                for (size_t i = idx(r, t); i < idx(r, t + 0.001); ++i)
                {
                    m = std::max(m, r.gain[i]);
                    lo = std::min(lo, r.gain[i]);
                }
                return 0.5f * (m + lo);
            };
            INFO("t=" << t << " atk=" << atk);
            CHECK(std::abs(win(r1) - win(r4)) <= 1.0f);
        }
    }
}

TEST_CASE("Silence, denormals and zero settings are safe", "[transient]")
{
    const std::vector<float> zeros(48000, 0.0f);
    const auto rz = runShaper(zeros, 48000.0, 12.0f, 12.0f, 0.5f);
    for (auto v : rz.out)
        REQUIRE(std::isfinite(v));

    std::vector<float> den(48000, 1.0e-40f);
    for (size_t i = 0; i < den.size(); i += 2)
        den[i] = -1.0e-40f;
    const auto rd = runShaper(den, 48000.0, 24.0f, -24.0f, 0.0f);
    for (size_t i = 0; i < rd.out.size(); ++i)
    {
        REQUIRE(std::isfinite(rd.out[i]));
        REQUIRE(std::isfinite(rd.gain[i]));
    }

    const auto in = makeBurst(48000.0, 0.6);
    const auto rt = runShaper(in, 48000.0, 0.0f, 0.0f, 0.5f);
    for (size_t i = idx(rt, 0.1); i < in.size(); ++i)
        REQUIRE(std::abs(rt.out[i] - in[i]) < 1.0e-6f);
}
