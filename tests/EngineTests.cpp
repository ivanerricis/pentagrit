// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestSignals.h"
#include "dsp/MultibandEngine.h"
#include "params/Parameters.h"
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

TEST_CASE("Drive compensation keeps the engine level of a low-level sine", "[engine]")
{
    const double sr = 48000.0;
    const size_t n = 48000;
    const auto in = sine(n, 500.0, sr, 0.001f);
    for (auto f : {OSFactor::x1, OSFactor::x2})
        for (auto sat : {SatType::tanh, SatType::tube, SatType::hardClip, SatType::wavefold})
        {
            auto s = neutral(f, 3);
            for (auto& b : s.bands)
            {
                b.sat = sat;
                b.driveDb = 24.0f;
            }
            auto levelDb = [&](bool comp)
            {
                s.driveComp = comp;
                auto e = makeEngine(sr, 512, 1, s);
                const auto out = renderMono(*e, s, in);
                return toDb(rms(out, n * 3 / 4, n)) - toDb(rms(in, n * 3 / 4, n));
            };
            const double off = levelDb(false), on = levelDb(true);
            // Reference: the same curve at 0 dB drive (what compensation restores).
            for (auto& b : s.bands)
                b.driveDb = 0.0f;
            const double ref = levelDb(false);
            INFO("os " << static_cast<int>(f) << " sat " << static_cast<int>(sat) << ": off " << off
                       << " dB, on " << on << " dB, drive 0 dB " << ref << " dB");
            CHECK(off > ref + 20.0);
            CHECK(std::abs(on - ref) < 0.15);
        }
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
            s.lookaheadMs =
                chance(0.5) ? 0.0f : kLookaheadMs[std::uniform_int_distribution<size_t>(1, 3)(gen)];
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

TEST_CASE("reset() clears every trace of the previous audio", "[engine][reset]")
{
    const double sr = 48000.0;
    const Channels loudAll{noise(8191, 21, 0.9f), noise(8191, 22, 0.9f)};
    const Channels silenceAll{Signal(2048, 0.0f), Signal(2048, 0.0f)};
    const Channels probeAll{noise(4096, 23, 0.3f), noise(4096, 24, 0.3f)};

    for (int variant = 0; variant < 4; ++variant)
    {
        // Variant 3 is mono; the others are stereo.
        const size_t numCh = variant == 3 ? 1 : 2;
        const Channels loud(loudAll.begin(), loudAll.begin() + static_cast<std::ptrdiff_t>(numCh));
        const Channels silence(silenceAll.begin(), silenceAll.begin() + static_cast<std::ptrdiff_t>(numCh));
        const Channels probe(probeAll.begin(), probeAll.begin() + static_cast<std::ptrdiff_t>(numCh));
        for (auto os : kFactors)
        {
            for (auto phase : {OSPhase::linear, OSPhase::minimum})
            {
                for (float lookaheadMs : {0.0f, 5.0f})
                {
                    EngineSettings s;
                    s.os = os;
                    s.osPhase = phase;
                    s.lookaheadMs = lookaheadMs;
                    s.autoGain = true;
                    s.driveComp = true;
                    for (size_t b = 0; b < s.bands.size(); ++b)
                    {
                        s.bands[b].attackDb = 9.0f;
                        s.bands[b].sustainDb = -6.0f;
                        s.bands[b].sat = static_cast<SatType>((b + 1) % 6);
                        s.bands[b].driveDb = 12.0f;
                    }
                    if (variant == 1) // partial mixes, fewer bands, a bypassed and a muted band
                    {
                        s.numBands = 4;
                        s.mix01 = 0.6f;
                        s.bands[0].mix01 = 0.5f;
                        s.bands[1].bypass = true;
                        s.bands[2].mute = true;
                        s.inGainDb = 6.0f;
                        s.outGainDb = -3.0f;
                    }
                    else if (variant == 2) // no auto-gain, no compensation, no saturation
                    {
                        s.autoGain = false;
                        s.driveComp = false;
                        for (auto& band : s.bands)
                        {
                            band.sat = SatType::off;
                        }
                    }
                    const int block = variant == 1 ? 480 : 512;

                    auto e = makeEngine(sr, 512, static_cast<int>(numCh), s);
                    render(*e, s, loud, block);
                    e->reset();
                    const auto quiet = render(*e, s, silence, block);
                    long firstIdx = -1;
                    float firstVal = 0.0f;
                    for (const auto& ch : quiet)
                    {
                        for (size_t i = 0; i < ch.size(); ++i)
                        {
                            if (ch[i] != 0.0f && (firstIdx < 0 || static_cast<long>(i) < firstIdx))
                            {
                                firstIdx = static_cast<long>(i);
                                firstVal = ch[i];
                            }
                        }
                    }
                    INFO("variant " << variant << " x" << (1 << static_cast<int>(os))
                                    << (phase == OSPhase::linear ? " linear" : " minimum") << " lookahead "
                                    << lookaheadMs << " ms: first non-zero sample after reset at index "
                                    << firstIdx << " = " << firstVal);
                    CHECK(firstIdx < 0);

                    // The audio that follows is bit-identical to what an engine that never saw the loud
                    // signal produces from the same (reset) state. The reference is reset as well so that the
                    // one-off fades of the very first parameter change after prepare() do not count as
                    // history.
                    auto fresh = makeEngine(sr, 512, static_cast<int>(numCh), s);
                    fresh->reset();
                    const auto expected = render(*fresh, s, probe, block);
                    e->reset();
                    render(*e, s, loud, block);
                    e->reset();
                    const auto fromReset = render(*e, s, probe, block);
                    long firstDiff = -1;
                    double maxDiff = 0.0;
                    for (size_t c = 0; c < expected.size(); ++c)
                    {
                        for (size_t i = 0; i < expected[c].size(); ++i)
                        {
                            const double d = std::abs(static_cast<double>(fromReset[c][i]) -
                                                      static_cast<double>(expected[c][i]));
                            if (d > 0.0 && firstDiff < 0)
                            {
                                firstDiff = static_cast<long>(i);
                            }
                            maxDiff = std::max(maxDiff, d);
                        }
                    }
                    INFO("variant " << variant << " x" << (1 << static_cast<int>(os))
                                    << (phase == OSPhase::linear ? " linear" : " minimum") << " lookahead "
                                    << lookaheadMs
                                    << " ms: first difference to a reset fresh engine at index " << firstDiff
                                    << ", max " << maxDiff);
                    CHECK(firstDiff < 0);
                }
            }
        }
    }
}

TEST_CASE("reset() leaves the same state as a freshly prepared engine", "[engine][reset]")
{
    // Default settings only (no parameter change after prepare() that would start a fade), so a freshly
    // prepared engine is a valid reference without calling reset() on it.
    const double sr = 48000.0;
    const Channels loud{noise(8191, 21, 0.9f), noise(8191, 22, 0.9f)};
    const Channels probe{noise(4096, 23, 0.3f), noise(4096, 24, 0.3f)};
    for (int numCh : {1, 2})
    {
        for (auto os : kFactors)
        {
            for (auto phase : {OSPhase::linear, OSPhase::minimum})
            {
                for (float lookaheadMs : {0.0f, 5.0f})
                {
                    EngineSettings s;
                    s.os = os;
                    s.osPhase = phase;
                    s.lookaheadMs = lookaheadMs;
                    const Channels loudIn(loud.begin(), loud.begin() + numCh);
                    const Channels probeIn(probe.begin(), probe.begin() + numCh);
                    auto fresh = makeEngine(sr, 512, numCh, s);
                    const auto expected = render(*fresh, s, probeIn, 512);
                    auto e = makeEngine(sr, 512, numCh, s);
                    render(*e, s, loudIn, 512);
                    e->reset();
                    const auto out = render(*e, s, probeIn, 512);
                    long firstDiff = -1;
                    double maxDiff = 0.0;
                    for (size_t c = 0; c < expected.size(); ++c)
                    {
                        for (size_t i = 0; i < expected[c].size(); ++i)
                        {
                            const double d = std::abs(static_cast<double>(out[c][i]) -
                                                      static_cast<double>(expected[c][i]));
                            if (d > 0.0 && firstDiff < 0)
                            {
                                firstDiff = static_cast<long>(i);
                            }
                            maxDiff = std::max(maxDiff, d);
                        }
                    }
                    INFO(numCh << " ch, x" << (1 << static_cast<int>(os))
                               << (phase == OSPhase::linear ? " linear" : " minimum") << " lookahead "
                               << lookaheadMs << " ms: first difference at index " << firstDiff << ", max "
                               << maxDiff);
                    CHECK(firstDiff < 0);
                }
            }
        }
    }
}

TEST_CASE("reset() after runtime reconfiguration and bypass leaves nothing behind", "[engine][reset]")
{
    const double sr = 48000.0;
    const Channels loud{noise(6000, 61, 0.9f), noise(6000, 62, 0.9f)};
    const Channels silence{Signal(4096, 0.0f), Signal(4096, 0.0f)};
    const Channels probe{noise(4096, 63, 0.3f), noise(4096, 64, 0.3f)};

    EngineSettings s;
    s.os = OSFactor::x2;
    s.autoGain = true;
    s.lookaheadMs = 2.0f;
    for (auto& b : s.bands)
    {
        b.attackDb = 6.0f;
        b.sat = SatType::tube;
        b.driveDb = 9.0f;
    }
    auto e = makeEngine(sr, 512, 2, s);
    render(*e, s, loud, 500);
    for (auto os : {OSFactor::x4, OSFactor::x2, OSFactor::x8, OSFactor::x2})
    {
        s.os = os;
        s.osPhase = os == OSFactor::x8 ? OSPhase::minimum : OSPhase::linear;
        s.numBands = 2 + static_cast<int>(os) % 4;
        render(*e, s, loud, 500);
    }
    s.bypass = true;
    render(*e, s, loud, 500);
    s.bypass = false;
    render(*e, s, loud, 500); // leaving bypass crossfades against the delayed input
    e->reset();
    const auto quiet = render(*e, s, silence, 500);
    for (const auto& ch : quiet)
    {
        for (size_t i = 0; i < ch.size(); ++i)
        {
            if (ch[i] != 0.0f)
            {
                FAIL("non-zero sample after reset at index " << i << ": " << ch[i]);
            }
        }
    }

    auto fresh = makeEngine(sr, 512, 2, s);
    fresh->reset();
    const auto expected = render(*fresh, s, probe, 500);
    e->reset();
    render(*e, s, loud, 500);
    e->reset();
    CHECK(render(*e, s, probe, 500) == expected);
}

namespace
{
EngineSettings autoGainSettings(OSFactor os, float lookaheadMs, float bandGainDb)
{
    EngineSettings s;
    s.os = os;
    s.lookaheadMs = lookaheadMs;
    s.autoGain = true;
    for (auto& b : s.bands)
    {
        b.gainDb = bandGainDb;
    }
    return s;
}

// Host-block loop that records the auto-gain meter after every block; `x` is processed in place.
std::vector<float> renderWithMeter(MultibandEngine& e, const EngineSettings& s, Signal& x, int block)
{
    std::vector<float> meterDb;
    for (size_t pos = 0; pos < x.size(); pos += static_cast<size_t>(block))
    {
        float* ptr = x.data() + pos;
        const int len = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), x.size() - pos));
        juce::AudioBuffer<float> buf(&ptr, 1, len);
        e.setSettings(s);
        e.process(buf);
        meterDb.push_back(e.meters().autoGainDb.load());
    }
    return meterDb;
}
} // namespace

TEST_CASE("AutoGain restores the level of the unprocessed path with oversampling and lookahead",
          "[engine][autogain]")
{
    const double sr = 48000.0;
    const size_t n = static_cast<size_t>(sr) * 8;
    const auto in = noise(n, 31, 0.2f);
    for (auto os : {OSFactor::x1, OSFactor::x2, OSFactor::x4})
    {
        for (float lookaheadMs : {0.0f, 5.0f})
        {
            // Reference: the same chain with neutral bands (the oversampler filters trim the top of the
            // noise).
            const auto neutralSettings = autoGainSettings(os, lookaheadMs, 0.0f);
            auto eRef = makeEngine(sr, 512, 1, neutralSettings);
            const auto refOut = renderMono(*eRef, neutralSettings, in);
            const double refDb = toDb(rms(refOut, n - 48000, n));
            for (float gainDb : {-12.0f, 9.0f})
            {
                const auto s = autoGainSettings(os, lookaheadMs, gainDb);
                auto e = makeEngine(sr, 512, 1, s);
                const auto out = renderMono(*e, s, in);
                const double outDb = toDb(rms(out, n - 48000, n));
                INFO("x" << (1 << static_cast<int>(os)) << " lookahead " << lookaheadMs << " ms, band gain "
                         << gainDb << " dB: out " << outDb << " dB, neutral " << refDb << " dB, meter "
                         << e->meters().autoGainDb.load());
                CHECK(std::abs(outDb - refDb) < 0.1);
                CHECK(e->meters().autoGainDb.load() == Approx(-gainDb).margin(0.1f));
            }
        }
    }
}

TEST_CASE("AutoGain reference is aligned with the processed signal at an onset", "[engine][autogain]")
{
    // Silence, then steady noise from `onset`. The wet path is delayed by the lookahead and the reference
    // must be delayed identically, so the gain trajectory with the lookahead is the one without it shifted by
    // exactly the lookahead. With a reference that is not delayed it runs 5 ms ahead of the wet signal and
    // the measured level ratio is wrong for the first milliseconds after the onset.
    const double sr = 48000.0;
    const size_t block = 16;
    const size_t onset = 4096;
    const size_t total = onset + 9600; // 200 ms after the onset (a multiple of the block size)
    const auto lookaheadSamples = static_cast<size_t>(MultibandEngine::lookaheadSamples(5.0f, sr));
    REQUIRE(lookaheadSamples % block == 0);
    const size_t shift = lookaheadSamples / block;
    Signal in(total, 0.0f);
    const auto sig = noise(total - onset, 41, 0.3f);
    std::copy(sig.begin(), sig.end(), in.begin() + static_cast<std::ptrdiff_t>(onset));

    for (auto os : {OSFactor::x1, OSFactor::x2})
    {
        auto trajectory = [&](float lookaheadMs)
        {
            const auto s = autoGainSettings(os, lookaheadMs, -12.0f);
            auto e = makeEngine(sr, 512, 1, s);
            Signal x = in;
            return renderWithMeter(*e, s, x, static_cast<int>(block));
        };
        const auto off = trajectory(0.0f);
        const auto la = trajectory(5.0f);
        float worstDiff = 0.0f, peak = 0.0f;
        for (size_t i = 0; i + shift < off.size(); ++i)
        {
            worstDiff = std::max(worstDiff, std::abs(off[i] - la[i + shift]));
            peak = std::max(peak, off[i]);
        }
        INFO("x" << (1 << static_cast<int>(os)) << ": worst difference to the shifted Off trajectory "
                 << worstDiff << " dB, Off gain reaches " << peak << " dB");
        CHECK(peak > 3.0f); // the compensation really moves (towards +12 dB)
        CHECK(worstDiff < 0.05f);
    }
}

TEST_CASE("AutoGain does not pump on bursty material", "[engine][autogain]")
{
    // 40 ms noise bursts every 250 ms over a -60 dB bed, band gain -6 dB. The compensation must settle on
    // +6 dB and stay there: no ripple at the burst rate.
    const double sr = 48000.0;
    const size_t n = static_cast<size_t>(sr) * 10;
    Signal in = noise(n, 51, 0.001f);
    const auto b = noise(n, 52, 0.5f);
    for (size_t start = 0; start + 1920 < n; start += 12000)
    {
        for (size_t i = start; i < start + 1920; ++i)
        {
            in[i] = b[i];
        }
    }
    for (auto os : {OSFactor::x1, OSFactor::x2})
    {
        for (float lookaheadMs : {0.0f, 5.0f})
        {
            const auto s = autoGainSettings(os, lookaheadMs, -6.0f);
            auto e = makeEngine(sr, 512, 1, s);
            Signal x = in;
            const auto meter = renderWithMeter(*e, s, x, 256);
            float lo = 100.0f, hi = -100.0f;
            for (size_t i = meter.size() / 2; i < meter.size(); ++i)
            {
                lo = std::min(lo, meter[i]);
                hi = std::max(hi, meter[i]);
            }
            INFO("x" << (1 << static_cast<int>(os)) << " lookahead " << lookaheadMs << " ms: gain range ["
                     << lo << ", " << hi << "] dB over the last 5 s");
            CHECK(hi - lo < 0.15f);
            CHECK(0.5f * (hi + lo) == Approx(6.0f).margin(0.15f));
        }
    }
}
