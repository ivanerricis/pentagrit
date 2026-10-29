// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestSignals.h"
#include "dsp/AutoGain.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace pentagrit;
using namespace pentagrit::test;

namespace
{
constexpr double fs = 48000.0;
constexpr size_t blk = 256;

// Feeds `seconds` of ref and wet = wetScale * ref through the module in blocks.
float runFor(AutoGain& ag, double seconds, float wetScale, float refAmp = 0.5f, bool silent = false)
{
    const size_t total = size_t(seconds * fs);
    const auto ref = silent ? Signal(total, 0.0f) : sine(total, 220.0, fs, refAmp);
    for (size_t pos = 0; pos < total; pos += blk)
    {
        const size_t n = std::min(blk, total - pos);
        Signal r(ref.begin() + std::ptrdiff_t(pos), ref.begin() + std::ptrdiff_t(pos + n));
        Signal w(n);
        for (size_t i = 0; i < n; ++i)
            w[i] = r[i] * wetScale;
        const float* rc[1] = {r.data()};
        float* wc[1] = {w.data()};
        ag.process(juce::dsp::AudioBlock<const float>(rc, 1, n), juce::dsp::AudioBlock<float>(wc, 1, n));
        for (float v : w)
            REQUIRE(std::isfinite(v));
    }
    return ag.getCurrentGainDb();
}
} // namespace

TEST_CASE("AutoGain converges to the level difference", "[AutoGain]")
{
    AutoGain ag;
    ag.prepare(fs, 512, 1);
    ag.setEnabled(true);
    const float g = runFor(ag, 2.0, 0.25f);
    CHECK(std::abs(g - 12.04f) < 0.5f);
}

TEST_CASE("AutoGain clamps at +24 dB", "[AutoGain]")
{
    AutoGain ag;
    ag.prepare(fs, 512, 1);
    ag.setEnabled(true);
    const float g = runFor(ag, 4.0, 0.01f);
    CHECK(std::abs(g - 24.0f) < 0.1f);
    CHECK(g <= 24.0f);
}

TEST_CASE("AutoGain holds in silence", "[AutoGain]")
{
    AutoGain ag;
    ag.prepare(fs, 512, 1);
    ag.setEnabled(true);
    const float before = runFor(ag, 2.0, 0.25f);
    const float after = runFor(ag, 3.0, 0.25f, 0.5f, true);
    CHECK(std::abs(after - before) < 0.3f);
}

TEST_CASE("AutoGain disabled ramps to 0 dB", "[AutoGain]")
{
    AutoGain ag;
    ag.prepare(fs, 512, 1);
    ag.setEnabled(true);
    CHECK(runFor(ag, 2.0, 0.25f) > 10.0f);
    ag.setEnabled(false);
    CHECK(runFor(ag, 3.0, 0.25f) == 0.0f);
}

TEST_CASE("AutoGain applies the gain to the wet signal", "[AutoGain]")
{
    AutoGain ag;
    ag.prepare(fs, 512, 1);
    ag.setEnabled(true);
    runFor(ag, 3.0, 0.25f);
    auto r = sine(blk, 220.0, fs, 0.5f);
    Signal w(blk);
    for (size_t i = 0; i < blk; ++i)
        w[i] = r[i] * 0.25f;
    const float* rc[1] = {r.data()};
    float* wc[1] = {w.data()};
    ag.process(juce::dsp::AudioBlock<const float>(rc, 1, blk), juce::dsp::AudioBlock<float>(wc, 1, blk));
    CHECK(std::abs(toDb(rms(w) / rms(r))) < 0.5);
}

TEST_CASE("AutoGain is safe with silence and denormals", "[AutoGain]")
{
    AutoGain ag;
    ag.prepare(96000.0, 512, 2);
    ag.setEnabled(true);
    Signal a(512, 1e-40f), b(512, 1e-40f), c(512, 0.0f), d(512, 0.0f);
    const float* rc[2] = {a.data(), c.data()};
    float* wc[2] = {b.data(), d.data()};
    for (int i = 0; i < 400; ++i)
        ag.process(juce::dsp::AudioBlock<const float>(rc, 2, 512), juce::dsp::AudioBlock<float>(wc, 2, 512));
    CHECK(std::isfinite(ag.getCurrentGainDb()));
    for (float v : b)
        REQUIRE(std::isfinite(v));
}
