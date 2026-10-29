// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestSignals.h"
#include "dsp/MultibandEngine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <random>

using namespace pentagrit;
using namespace pentagrit::test;
using Catch::Approx;

namespace
{
using Channels = std::vector<Signal>;

std::unique_ptr<MultibandEngine> makeEngine(double sr, int maxBlock, int numCh, const EngineSettings& s)
{
    auto e = std::make_unique<MultibandEngine>();
    e->prepare(sr, maxBlock, numCh);
    e->setSettings(s);
    return e;
}

// Processes `in` in host blocks of `block` samples; settings are re-applied every block like the plugin does.
Channels render(MultibandEngine& e, const EngineSettings& s, const Channels& in, int block,
                bool nonRealtime = false)
{
    Channels out = in;
    const int total = static_cast<int>(in[0].size());
    std::vector<float*> ptrs(out.size());
    for (int pos = 0; pos < total; pos += block)
    {
        const int len = std::min(block, total - pos);
        for (size_t c = 0; c < out.size(); ++c)
            ptrs[c] = out[c].data();
        juce::AudioBuffer<float> buf(ptrs.data(), static_cast<int>(out.size()), pos, len);
        e.setNonRealtime(nonRealtime);
        e.setSettings(s);
        e.process(buf);
    }
    return out;
}

Signal renderMono(MultibandEngine& e, const EngineSettings& s, const Signal& in, int block = 512)
{
    return render(e, s, Channels{in}, block)[0];
}

EngineSettings neutral(OSFactor os, int numBands = 3)
{
    EngineSettings s;
    s.os = os;
    s.numBands = numBands;
    return s;
}

double steadyGainDb(MultibandEngine& e, const EngineSettings& s, double freq, double sr)
{
    e.reset();
    const auto n = static_cast<size_t>(sr);
    const auto in = sine(n, freq, sr, 0.5f);
    const auto out = renderMono(e, s, in);
    const size_t from = n * 3 / 4;
    return toDb(rms(out, from, n)) - toDb(rms(in, from, n));
}

constexpr OSFactor kFactors[] = {OSFactor::x1, OSFactor::x2, OSFactor::x4, OSFactor::x8};
constexpr int kFactorValues[] = {1, 2, 4, 8};

// Lag (samples) at which `b` best matches `a` delayed, i.e. argmax_k sum a[n] * b[n + k].
int bestLag(const Signal& a, const Signal& b, int maxLag)
{
    int best = 0;
    double bestVal = -1.0e30;
    const size_t n = a.size() - static_cast<size_t>(maxLag);
    for (int k = 0; k <= maxLag; ++k)
    {
        double acc = 0.0;
        for (size_t i = 2048; i < n; ++i)
            acc += static_cast<double>(a[i]) * static_cast<double>(b[i + static_cast<size_t>(k)]);
        if (acc > bestVal)
        {
            bestVal = acc;
            best = k;
        }
    }
    return best;
}

// Energy outside DC, the fundamental (bin 3715 of 32768 at 44.1 kHz = 4999.8 Hz) and its legitimate 3rd
// harmonic.
double aliasEnergy(const Signal& x)
{
    const size_t n = x.size();
    Signal w(n);
    for (size_t i = 0; i < n; ++i)
        w[i] = x[i] * static_cast<float>(
                          0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n)));
    const auto spec = spectrum(w);
    const int k0 = 3715;
    double alias = 0.0;
    for (size_t k = 0; k < spec.size(); ++k)
    {
        const int ki = static_cast<int>(k);
        const bool legit = ki < 12 || std::abs(ki - k0) < 12 || std::abs(ki - 3 * k0) < 12;
        if (!legit)
            alias += std::norm(spec[k]);
    }
    return alias;
}
} // namespace

TEST_CASE("computeAudibility truth table", "[engine]")
{
    EngineSettings s;
    s.numBands = 3;

    auto aud = [&] { return MultibandEngine::computeAudibility(s); };

    SECTION("nothing muted or soloed")
    {
        const auto a = aud();
        CHECK(a[0]);
        CHECK(a[1]);
        CHECK(a[2]);
        CHECK_FALSE(a[3]);
        CHECK_FALSE(a[4]);
    }
    SECTION("mute")
    {
        s.bands[1].mute = true;
        const auto a = aud();
        CHECK(a[0]);
        CHECK_FALSE(a[1]);
        CHECK(a[2]);
    }
    SECTION("solo wins over everything else")
    {
        s.bands[2].solo = true;
        const auto a = aud();
        CHECK_FALSE(a[0]);
        CHECK_FALSE(a[1]);
        CHECK(a[2]);
    }
    SECTION("solo and mute on the same band: solo wins")
    {
        s.bands[0].solo = true;
        s.bands[0].mute = true;
        const auto a = aud();
        CHECK(a[0]);
        CHECK_FALSE(a[1]);
    }
    SECTION("several solos")
    {
        s.bands[0].solo = true;
        s.bands[2].solo = true;
        s.bands[1].mute = true;
        const auto a = aud();
        CHECK(a[0]);
        CHECK_FALSE(a[1]);
        CHECK(a[2]);
    }
    SECTION("solo on an inactive band is ignored, inactive bands are never audible")
    {
        s.bands[4].solo = true;
        const auto a = aud();
        CHECK(a[0]);
        CHECK(a[1]);
        CHECK(a[2]);
        CHECK_FALSE(a[3]);
        CHECK_FALSE(a[4]);
    }
}

TEST_CASE("effectiveFactor respects the 384 kHz cap", "[engine]")
{
    CHECK(MultibandEngine::effectiveFactor(OSFactor::x8, 44100.0) == 8);
    CHECK(MultibandEngine::effectiveFactor(OSFactor::x8, 48000.0) == 8);
    CHECK(MultibandEngine::effectiveFactor(OSFactor::x8, 96000.0) == 4);
    CHECK(MultibandEngine::effectiveFactor(OSFactor::x8, 192000.0) == 2);
    CHECK(MultibandEngine::effectiveFactor(OSFactor::x4, 192000.0) == 2);
    CHECK(MultibandEngine::effectiveFactor(OSFactor::x2, 192000.0) == 2);
    CHECK(MultibandEngine::effectiveFactor(OSFactor::x1, 192000.0) == 1);
}

TEST_CASE("Default engine is flat 20 Hz - 18 kHz at every oversampling factor", "[engine]")
{
    const double sr = 48000.0;
    const double freqs[] = {20, 31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000, 18000};

    for (int bands : {3, 5})
        for (auto f : kFactors)
        {
            DYNAMIC_SECTION("bands " << bands << " factor " << (1 << static_cast<int>(f)))
            {
                const auto s = neutral(f, bands);
                auto e = makeEngine(sr, 512, 1, s);
                for (double hz : freqs)
                {
                    const double g = steadyGainDb(*e, s, hz, sr);
                    INFO("freq " << hz << " gain " << g << " dB");
                    CHECK(std::abs(g) <= 0.1);
                }
            }
        }
}

TEST_CASE("Mix 0.5 with neutral bands is flat", "[engine]")
{
    const double sr = 48000.0;
    for (auto f : {OSFactor::x1, OSFactor::x2, OSFactor::x4})
    {
        auto s = neutral(f);
        s.mix01 = 0.5f;
        auto e = makeEngine(sr, 512, 1, s);
        for (double hz : {30.0, 200.0, 1000.0, 5000.0, 15000.0})
        {
            const double g = steadyGainDb(*e, s, hz, sr);
            INFO("freq " << hz << " gain " << g << " dB");
            CHECK(std::abs(g) <= 0.1);
        }
    }
}

TEST_CASE("Global mix 0 returns the phase matched dry", "[engine]")
{
    const double sr = 48000.0;
    const auto in = noise(16384, 7, 0.3f);

    auto ref = neutral(OSFactor::x2, 4);
    auto e1 = makeEngine(sr, 512, 1, ref);
    const auto expected = renderMono(*e1, ref, in);

    auto wild = ref;
    wild.mix01 = 0.0f;
    for (auto& b : wild.bands)
    {
        b.sat = SatType::hardClip;
        b.driveDb = 30.0f;
        b.attackDb = 12.0f;
        b.gainDb = 6.0f;
    }
    auto e2 = makeEngine(sr, 512, 1, wild);
    const auto out = renderMono(*e2, wild, in);

    // Both paths are the same allpass up to rounding.
    double err = 0.0, sig = 0.0;
    for (size_t i = 4096; i < in.size(); ++i)
    {
        const double d = static_cast<double>(out[i]) - static_cast<double>(expected[i]);
        err += d * d;
        sig += static_cast<double>(expected[i]) * static_cast<double>(expected[i]);
    }
    CHECK(10.0 * std::log10(err / sig) < -60.0);
}

TEST_CASE("Bypass is the input delayed by exactly the reported latency", "[engine]")
{
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0})
        for (size_t fi = 0; fi < 4; ++fi)
        {
            DYNAMIC_SECTION("sr " << sr << " factor " << kFactorValues[fi])
            {
                auto s = neutral(kFactors[fi]);
                s.bypass = true;
                auto e = makeEngine(sr, 512, 1, s);
                const int expectedFactor = MultibandEngine::effectiveFactor(kFactors[fi], sr);
                const int latency = e->getLatencySamples();
                if (expectedFactor == 1)
                    CHECK(latency == 0);
                else
                    CHECK(latency > 0);
                CHECK(e->meters().latencySamples.load() == latency);

                const size_t pos = 100;
                const auto in = impulse(2048 + static_cast<size_t>(latency), pos, 1.0f);
                const auto out = renderMono(*e, s, in, 300);
                for (size_t i = 0; i < out.size(); ++i)
                {
                    const float expected = i == pos + static_cast<size_t>(latency) ? 1.0f : 0.0f;
                    REQUIRE(out[i] == expected);
                }
            }
        }
}

TEST_CASE("Bypass is bit exact (delay only) for arbitrary signals", "[engine]")
{
    auto s = neutral(OSFactor::x4);
    s.bypass = true;
    auto e = makeEngine(48000.0, 256, 2, s);
    const int latency = e->getLatencySamples();
    const Channels in{noise(5000, 1), noise(5000, 2)};
    const auto out = render(*e, s, in, 77);
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < in[c].size(); ++i)
        {
            const float expected =
                i < static_cast<size_t>(latency) ? 0.0f : in[c][i - static_cast<size_t>(latency)];
            REQUIRE(out[c][i] == expected);
        }
}

TEST_CASE("Toggling bypass crossfades and then is bit exact", "[engine]")
{
    const double sr = 48000.0;
    auto s = neutral(OSFactor::x2);
    auto e = makeEngine(sr, 512, 1, s);
    const int latency = e->getLatencySamples();
    const auto in = sine(static_cast<size_t>(sr), 440.0, sr, 0.5f);

    Signal out(in.size());
    const size_t half = in.size() / 2;
    {
        const Signal first(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(half));
        const auto a = renderMono(*e, s, first);
        std::copy(a.begin(), a.end(), out.begin());
        s.bypass = true;
        const Signal second(in.begin() + static_cast<std::ptrdiff_t>(half), in.end());
        const auto b = renderMono(*e, s, second);
        std::copy(b.begin(), b.end(), out.begin() + static_cast<std::ptrdiff_t>(half));
    }
    // After the 20 ms crossfade the output is exactly the delayed input.
    for (size_t i = half + static_cast<size_t>(0.025 * sr); i < in.size(); ++i)
        REQUIRE(out[i] == in[i - static_cast<size_t>(latency)]);
    // No jump anywhere (a 440 Hz sine at 0.5 changes by < 0.03 per sample).
    for (size_t i = 1; i < out.size(); ++i)
        REQUIRE(std::abs(out[i] - out[i - 1]) < 0.08f);
}

TEST_CASE("Reported latency matches the measured latency", "[engine]")
{
    const double sr = 48000.0;
    const auto in = noise(16384, 99, 0.25f);

    auto s1 = neutral(OSFactor::x1);
    auto e1 = makeEngine(sr, 512, 1, s1);
    const auto ref = renderMono(*e1, s1, in);

    for (auto phase : {OSPhase::linear, OSPhase::minimum})
        for (size_t fi = 1; fi < 4; ++fi)
        {
            DYNAMIC_SECTION((phase == OSPhase::linear ? "linear" : "minimum") << " x" << kFactorValues[fi])
            {
                auto s = neutral(kFactors[fi]);
                s.osPhase = phase;
                auto e = makeEngine(sr, 512, 1, s);
                const int latency = e->getLatencySamples();
                REQUIRE(latency > 0);
                const auto out = renderMono(*e, s, in);
                const int lag = bestLag(ref, out, 400);
                INFO("reported " << latency << " measured " << lag);
                if (phase == OSPhase::linear)
                    CHECK(lag == latency);
                else
                    CHECK(std::abs(lag - latency) <= 1);
            }
        }
}

TEST_CASE("Minimum phase works and has a lower latency than linear phase", "[engine]")
{
    const double sr = 48000.0;
    auto lin = neutral(OSFactor::x4);
    auto min = lin;
    min.osPhase = OSPhase::minimum;
    auto el = makeEngine(sr, 512, 1, lin);
    auto em = makeEngine(sr, 512, 1, min);
    CHECK(em->getLatencySamples() > 0);
    CHECK(em->getLatencySamples() < el->getLatencySamples());

    for (double hz : {100.0, 1000.0, 10000.0})
        CHECK(std::abs(steadyGainDb(*em, min, hz, sr)) < 0.5);
}

TEST_CASE("Switching phase at runtime updates the latency", "[engine]")
{
    auto s = neutral(OSFactor::x2);
    auto e = makeEngine(48000.0, 512, 1, s);
    const int lin = e->getLatencySamples();
    s.osPhase = OSPhase::minimum;
    e->setSettings(s);
    CHECK(e->getLatencySamples() != lin);
    CHECK(e->meters().latencySamples.load() == e->getLatencySamples());
}

TEST_CASE("Render factor is used when non-realtime", "[engine]")
{
    const double sr = 48000.0;
    auto live = neutral(OSFactor::x1);
    live.osRender = OSFactor::x4;
    live.osRenderSame = false;

    auto x4 = neutral(OSFactor::x4);
    auto refEngine = makeEngine(sr, 512, 1, x4);
    const int x4Latency = refEngine->getLatencySamples();
    REQUIRE(x4Latency > 0);

    auto e = makeEngine(sr, 512, 1, live);
    CHECK(e->getLatencySamples() == 0);
    e->setNonRealtime(true);
    CHECK(e->getLatencySamples() == x4Latency);
    e->setNonRealtime(false);
    CHECK(e->getLatencySamples() == 0);

    SECTION("Same as live ignores the render factor")
    {
        live.osRenderSame = true;
        e->setSettings(live);
        e->setNonRealtime(true);
        CHECK(e->getLatencySamples() == 0);
    }
    SECTION("render output matches a live x4 engine")
    {
        const auto in = noise(4096, 5, 0.3f);
        auto a = makeEngine(sr, 512, 1, live);
        const auto outRender = render(*a, live, Channels{in}, 512, true)[0];
        const auto outX4 = renderMono(*refEngine, x4, in, 512);
        for (size_t i = 0; i < in.size(); ++i)
            REQUIRE(outRender[i] == Approx(outX4[i]).margin(1e-6));
    }
}

TEST_CASE("Hard clipping aliases far less at 8x than at 1x", "[engine]")
{
    const double sr = 44100.0;
    const size_t n = 32768;
    const size_t warm = 16384;
    const auto in = sine(n + warm, 3715.0 * sr / static_cast<double>(n), sr, 0.5f);

    auto run = [&](OSFactor f)
    {
        auto s = neutral(f);
        for (auto& b : s.bands)
        {
            b.sat = SatType::hardClip;
            b.driveDb = 24.0f;
        }
        auto e = makeEngine(sr, 512, 1, s);
        const auto out = renderMono(*e, s, in);
        return aliasEnergy(Signal(out.begin() + static_cast<std::ptrdiff_t>(warm), out.end()));
    };

    const double a1 = run(OSFactor::x1);
    const double a8 = run(OSFactor::x8);
    const double reductionDb = 10.0 * std::log10(a1 / std::max(a8, 1.0e-30));
    INFO("alias reduction " << reductionDb << " dB");
    CHECK(reductionDb >= 20.0);
}

TEST_CASE("Output does not depend on the host block size", "[engine]")
{
    const double sr = 48000.0;
    EngineSettings s;
    s.os = OSFactor::x2;
    s.numBands = 4;
    s.inGainDb = 3.0f;
    s.outGainDb = -2.0f;
    s.mix01 = 0.8f;
    s.bands[0].sat = SatType::tanh;
    s.bands[0].driveDb = 12.0f;
    s.bands[1].attackDb = 9.0f;
    s.bands[1].sustainDb = -6.0f;
    s.bands[2].sat = SatType::tube;
    s.bands[2].mix01 = 0.6f;
    s.bands[2].gainDb = 2.0f;
    s.bands[3].mute = true;

    const Channels in{noise(6000, 11, 0.4f), noise(6000, 12, 0.4f)};
    auto ref = makeEngine(sr, 4096, 2, s);
    const auto expected = render(*ref, s, in, 4096);

    for (int block : {1, 7, 64})
    {
        DYNAMIC_SECTION("block " << block)
        {
            auto e = makeEngine(sr, 4096, 2, s);
            const auto out = render(*e, s, in, block);
            for (size_t c = 0; c < 2; ++c)
                for (size_t i = 0; i < in[c].size(); ++i)
                    REQUIRE(std::abs(out[c][i] - expected[c][i]) <= 1.0e-6f);
        }
    }

    SECTION("host blocks larger than the prepared block size are chunked internally")
    {
        auto e = makeEngine(sr, 512, 2, s);
        const auto out = render(*e, s, in, 6000);
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < in[c].size(); ++i)
                REQUIRE(std::abs(out[c][i] - expected[c][i]) <= 1.0e-6f);
    }
}

TEST_CASE("Mono equals stereo with identical channels", "[engine]")
{
    EngineSettings s;
    s.os = OSFactor::x2;
    s.bands[0].sat = SatType::tanh;
    s.bands[1].attackDb = 8.0f;
    const auto x = noise(4096, 21, 0.4f);
    auto m = makeEngine(48000.0, 512, 1, s);
    auto st = makeEngine(48000.0, 512, 2, s);
    const auto outM = render(*m, s, Channels{x}, 256)[0];
    const auto outS = render(*st, s, Channels{x, x}, 256);
    for (size_t i = 0; i < x.size(); ++i)
    {
        REQUIRE(outS[0][i] == Approx(outM[i]).margin(1e-6));
        REQUIRE(outS[1][i] == Approx(outM[i]).margin(1e-6));
    }
}

TEST_CASE("Mute, solo and band bypass act on the right bands", "[engine]")
{
    const double sr = 48000.0;
    auto s = neutral(OSFactor::x1);

    SECTION("muting the band that holds the signal")
    {
        s.bands[2].mute = true; // > 1200 Hz
        auto e = makeEngine(sr, 512, 1, s);
        CHECK(steadyGainDb(*e, s, 6000.0, sr) < -40.0);
        CHECK(std::abs(steadyGainDb(*e, s, 60.0, sr)) < 3.0);
    }
    SECTION("soloing another band")
    {
        s.bands[0].solo = true; // < 120 Hz
        auto e = makeEngine(sr, 512, 1, s);
        CHECK(steadyGainDb(*e, s, 6000.0, sr) < -40.0);
        CHECK(steadyGainDb(*e, s, 30.0, sr) > -3.0);
    }
    SECTION("band bypass skips the processing")
    {
        s.bands[2].sat = SatType::hardClip;
        s.bands[2].driveDb = 36.0f;
        s.bands[2].bypass = true;
        auto e = makeEngine(sr, 512, 1, s);
        CHECK(std::abs(steadyGainDb(*e, s, 6000.0, sr)) < 0.1);
    }
}

TEST_CASE("Meters report latency, crossovers and band activity", "[engine]")
{
    const double sr = 48000.0;
    auto s = neutral(OSFactor::x2, 3);
    s.xoverHz = {1000.0f, 1100.0f, 3000.0f, 8000.0f}; // too close: the engine enforces 1/3 octave
    auto e = makeEngine(sr, 512, 1, s);
    const auto in = sine(8192, 5000.0, sr, 0.5f);
    renderMono(*e, s, in);

    auto& m = e->meters();
    CHECK(m.latencySamples.load() == e->getLatencySamples());
    CHECK(m.effectiveXoverHz[1].load() >= m.effectiveXoverHz[0].load() * 1.25f);
    CHECK(m.inPeakDb.load() == Approx(-6.0f).margin(0.2f));
    CHECK(m.outPeakDb.load() == Approx(-6.0f).margin(0.5f));
    CHECK(m.bandPeakDb[2].load() > -7.0f);
    CHECK(m.bandPeakDb[0].load() < -40.0f);
    CHECK(m.bandPeakDb[3].load() == Approx(-100.0f));
    CHECK(m.bandPeakDb[4].load() == Approx(-100.0f));
}

TEST_CASE("AutoGain compensates a level change of the processing", "[engine]")
{
    const double sr = 48000.0;
    auto s = neutral(OSFactor::x1);
    for (auto& b : s.bands)
        b.gainDb = 12.0f;
    s.autoGain = true;
    auto e = makeEngine(sr, 512, 1, s);
    const auto in = noise(static_cast<size_t>(sr) * 4, 3, 0.2f);
    const auto out = renderMono(*e, s, in);
    const size_t n = in.size();
    const double diff = toDb(rms(out, n * 3 / 4, n)) - toDb(rms(in, n * 3 / 4, n));
    CHECK(std::abs(diff) < 1.0);
    CHECK(e->meters().autoGainDb.load() == Approx(-12.0f).margin(1.5f));
}

TEST_CASE("Changing band count and oversampling at runtime stays finite", "[engine]")
{
    auto s = neutral(OSFactor::x2, 5);
    auto e = makeEngine(48000.0, 512, 2, s);
    const Channels in{noise(512, 4, 0.5f), noise(512, 5, 0.5f)};
    for (int i = 0; i < 40; ++i)
    {
        s.numBands = 2 + i % 4;
        s.os = kFactors[(i / 2) % 4];
        s.osPhase = (i % 3 == 0) ? OSPhase::minimum : OSPhase::linear;
        const auto out = render(*e, s, in, 512);
        for (auto& ch : out)
            for (float v : ch)
                REQUIRE(std::isfinite(v));
    }
}

TEST_CASE("Fuzz: random parameters every block never produce NaN, Inf or huge peaks", "[engine][fuzz]")
{
    const double sr = 48000.0;
    const int block = 480;
    const int numBlocks = 1000; // 10 s
    std::mt19937 gen(20240607);
    auto uni = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(gen); };
    auto chance = [&](double p) { return std::bernoulli_distribution(p)(gen); };

    EngineSettings s;
    auto e = makeEngine(sr, 512, 2, s);
    std::vector<float> l(block), r(block);
    float worst = 0.0f;

    for (int blk = 0; blk < numBlocks; ++blk)
    {
        s.inGainDb = uni(-24.0f, 12.0f);
        s.outGainDb = uni(-24.0f, 12.0f);
        s.mix01 = uni(0.0f, 1.0f);
        s.autoGain = chance(0.5);
        s.bypass = chance(0.05);
        if (chance(0.1))
        {
            s.os = kFactors[std::uniform_int_distribution<int>(0, 3)(gen)];
            s.osPhase = chance(0.5) ? OSPhase::minimum : OSPhase::linear;
            s.numBands = std::uniform_int_distribution<int>(2, 5)(gen);
        }
        for (auto& x : s.xoverHz)
            x = std::exp(uni(std::log(20.0f), std::log(20000.0f)));
        for (auto& b : s.bands)
        {
            b.gainDb = uni(-24.0f, 12.0f);
            b.attackDb = uni(-24.0f, 24.0f);
            b.sustainDb = uni(-24.0f, 24.0f);
            b.speed01 = uni(0.0f, 1.0f);
            b.sat = static_cast<SatType>(std::uniform_int_distribution<int>(0, 5)(gen));
            b.driveDb = uni(0.0f, 36.0f);
            b.mix01 = uni(0.0f, 1.0f);
            b.mute = chance(0.1);
            b.solo = chance(0.1);
            b.bypass = chance(0.1);
        }
        const float amp = chance(0.2) ? 0.0f : uni(0.01f, 0.5f);
        for (int i = 0; i < block; ++i)
        {
            l[static_cast<size_t>(i)] = amp * uni(-1.0f, 1.0f);
            r[static_cast<size_t>(i)] = amp * uni(-1.0f, 1.0f);
        }
        float* ptrs[2] = {l.data(), r.data()};
        juce::AudioBuffer<float> buf(ptrs, 2, block);
        e->setSettings(s);
        e->process(buf);
        for (int i = 0; i < block; ++i)
        {
            const float a = l[static_cast<size_t>(i)], b = r[static_cast<size_t>(i)];
            REQUIRE(std::isfinite(a));
            REQUIRE(std::isfinite(b));
            worst = std::max({worst, std::abs(a), std::abs(b)});
        }
    }
    INFO("worst peak " << worst);
    CHECK(worst < 1000.0f); // +60 dBFS
}

TEST_CASE("Engine CPU cost per oversampling factor", "[.perf]")
{
    const double sr = 48000.0;
    const int block = 512;
    const int numBlocks = static_cast<int>(sr * 10 / block);
    for (auto f : kFactors)
    {
        EngineSettings s;
        s.os = f;
        s.numBands = 5;
        for (auto& b : s.bands)
        {
            b.sat = SatType::tube;
            b.attackDb = 6.0f;
        }
        s.autoGain = true;
        auto e = makeEngine(sr, block, 2, s);
        const auto x = noise(static_cast<size_t>(block), 1, 0.3f);
        juce::AudioBuffer<float> buf(2, block);
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < numBlocks; ++i)
        {
            for (int c = 0; c < 2; ++c)
                std::copy(x.begin(), x.end(), buf.getWritePointer(c));
            e->setSettings(s);
            e->process(buf);
        }
        const double secs =
            juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
        WARN("factor x" << (1 << static_cast<int>(f)) << ": " << (100.0 * secs / 10.0)
                        << " % of one core (5 bands, stereo, 48 kHz)");
    }
}
