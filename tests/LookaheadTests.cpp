// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "TestSignals.h"
#include "dsp/MultibandEngine.h"
#include "params/Parameters.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

using namespace pentagrit;
using namespace pentagrit::test;
using Catch::Approx;

namespace
{
using Channels = std::vector<Signal>;

constexpr float kLookaheads[] = {1.0f, 2.0f, 5.0f};
constexpr OSFactor kFactors[] = {OSFactor::x1, OSFactor::x2, OSFactor::x4};
constexpr int kFactorValues[] = {1, 2, 4};

std::unique_ptr<MultibandEngine> makeEngine(double sr, int maxBlock, int numCh, const EngineSettings& s)
{
    auto e = std::make_unique<MultibandEngine>();
    e->prepare(sr, maxBlock, numCh);
    e->setSettings(s);
    return e;
}

Channels render(MultibandEngine& e, const EngineSettings& s, const Channels& in, int block)
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
        e.setSettings(s);
        e.process(buf);
    }
    return out;
}

Signal renderMono(MultibandEngine& e, const EngineSettings& s, const Signal& in, int block = 512)
{
    return render(e, s, Channels{in}, block)[0];
}

EngineSettings neutral(OSFactor os, float lookaheadMs = 0.0f, int numBands = 3)
{
    EngineSettings s;
    s.os = os;
    s.numBands = numBands;
    s.lookaheadMs = lookaheadMs;
    return s;
}

int lookaheadSamples(float ms, double sr)
{
    return MultibandEngine::lookaheadSamples(ms, sr);
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

// Residual energy (dB, relative to the reference) of `x` against `ref` delayed by `delay` samples.
double residualDb(const Signal& ref, const Signal& x, size_t delay, size_t from)
{
    double err = 0.0, sig = 0.0;
    for (size_t i = from; i < x.size(); ++i)
    {
        const double r = i >= delay ? static_cast<double>(ref[i - delay]) : 0.0;
        const double d = static_cast<double>(x[i]) - r;
        err += d * d;
        sig += r * r;
    }
    return 10.0 * std::log10((err + 1.0e-30) / (sig + 1.0e-30));
}

// Gain (dB) the shaper applies in the first millisecond of a burst: RMS of the shaped render over the RMS of
// the neutral render with the same settings (so the crossover smear cancels), both read at the burst onset
// delayed by the reported latency.
double firstMsGainDb(OSFactor os, float lookaheadMs, float attackDb, double sr = 48000.0)
{
    const size_t onset = 9600;
    const auto in = burst(24000, onset, 9600, 1000.0, sr, 0.5f);

    auto flat = neutral(os, lookaheadMs);
    auto shaped = flat;
    for (auto& b : shaped.bands)
        b.attackDb = attackDb;

    auto e0 = makeEngine(sr, 512, 1, flat);
    const auto ref = renderMono(*e0, flat, in);
    auto e1 = makeEngine(sr, 512, 1, shaped);
    const int latency = e1->getLatencySamples();
    const auto out = renderMono(*e1, shaped, in);

    const size_t from = onset + static_cast<size_t>(latency);
    const size_t to = from + static_cast<size_t>(0.001 * sr);
    return toDb(rms(out, from, to)) - toDb(rms(ref, from, to));
}

void setChoice(PentagritAudioProcessor& p, const juce::String& id, int index)
{
    auto* param = p.getAPVTS().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(index)));
}
} // namespace

TEST_CASE("Lookahead sample counts are whole base-rate samples", "[lookahead]")
{
    CHECK(lookaheadSamples(0.0f, 48000.0) == 0);
    CHECK(lookaheadSamples(1.0f, 48000.0) == 48);
    CHECK(lookaheadSamples(2.0f, 48000.0) == 96);
    CHECK(lookaheadSamples(5.0f, 48000.0) == 240);
    CHECK(lookaheadSamples(1.0f, 44100.0) == 44);
    CHECK(lookaheadSamples(5.0f, 44100.0) == 221);
    CHECK(lookaheadSamples(5.0f, 192000.0) == 960);
    CHECK(lookaheadSamples(-3.0f, 48000.0) == 0);
    CHECK(lookaheadSamples(50.0f, 48000.0) == 240);
    CHECK(lookaheadSamples(std::nanf(""), 48000.0) == 0);
}

TEST_CASE("Lookahead adds exactly its time to the reported and the measured latency", "[lookahead]")
{
    const double sr = 48000.0;
    const auto in = noise(16384, 99, 0.25f);

    auto s1 = neutral(OSFactor::x1);
    auto e1 = makeEngine(sr, 512, 1, s1);
    const auto ref = renderMono(*e1, s1, in);

    for (auto phase : {OSPhase::linear, OSPhase::minimum})
        for (size_t fi = 0; fi < 3; ++fi)
        {
            auto off = neutral(kFactors[fi]);
            off.osPhase = phase;
            auto eOff = makeEngine(sr, 512, 1, off);
            const int latencyOff = eOff->getLatencySamples();
            const auto outOff = renderMono(*eOff, off, in);

            for (float ms : kLookaheads)
            {
                DYNAMIC_SECTION((phase == OSPhase::linear ? "linear" : "minimum")
                                << " x" << kFactorValues[fi] << " lookahead " << ms << " ms")
                {
                    auto s = off;
                    s.lookaheadMs = ms;
                    auto e = makeEngine(sr, 512, 1, s);
                    const int lookahead = lookaheadSamples(ms, sr);
                    const int latency = e->getLatencySamples();
                    CHECK(latency == latencyOff + lookahead);
                    CHECK(e->meters().latencySamples.load() == latency);

                    const auto out = renderMono(*e, s, in);
                    const int lag = bestLag(ref, out, 800);
                    INFO("reported " << latency << " measured " << lag);
                    if (phase == OSPhase::linear)
                        CHECK(lag == latency);
                    else
                        CHECK(std::abs(lag - latency) <= 1);

                    // The lookahead is a pure delay of the Off render.
                    const double res = residualDb(outOff, out, static_cast<size_t>(lookahead), 2048);
                    INFO("residual against the delayed Off render " << res << " dB");
                    CHECK(res < -120.0);
                }
            }
        }
}

TEST_CASE("Lookahead latency follows the sample rate and runtime changes", "[lookahead]")
{
    for (double sr : {44100.0, 96000.0})
    {
        auto s = neutral(OSFactor::x2);
        auto e = makeEngine(sr, 512, 1, s);
        const int base = e->getLatencySamples();
        for (float ms : {5.0f, 1.0f, 0.0f, 2.0f})
        {
            s.lookaheadMs = ms;
            e->setSettings(s);
            CHECK(e->getLatencySamples() == base + lookaheadSamples(ms, sr));
            CHECK(e->meters().latencySamples.load() == e->getLatencySamples());
        }
    }
}

TEST_CASE("Bypass with lookahead is the input delayed by exactly the reported latency", "[lookahead]")
{
    for (double sr : {44100.0, 48000.0})
        for (size_t fi = 0; fi < 3; ++fi)
            for (float ms : kLookaheads)
            {
                DYNAMIC_SECTION("sr " << sr << " x" << kFactorValues[fi] << " lookahead " << ms << " ms")
                {
                    auto s = neutral(kFactors[fi], ms);
                    s.bypass = true;
                    auto e = makeEngine(sr, 256, 2, s);
                    const int latency = e->getLatencySamples();
                    CHECK(latency >= lookaheadSamples(ms, sr));
                    const Channels in{noise(6000, 1), noise(6000, 2)};
                    const auto out = render(*e, s, in, 77);
                    for (size_t c = 0; c < 2; ++c)
                        for (size_t i = 0; i < in[c].size(); ++i)
                        {
                            const float expected = i < static_cast<size_t>(latency)
                                                       ? 0.0f
                                                       : in[c][i - static_cast<size_t>(latency)];
                            REQUIRE(out[c][i] == expected);
                        }
                }
            }
}

TEST_CASE("Toggling bypass with lookahead crossfades and then is bit exact", "[lookahead]")
{
    const double sr = 48000.0;
    auto s = neutral(OSFactor::x2, 5.0f);
    auto e = makeEngine(sr, 512, 1, s);
    const int latency = e->getLatencySamples();
    const auto in = sine(static_cast<size_t>(sr), 440.0, sr, 0.5f);

    Signal out(in.size());
    const size_t half = in.size() / 2;
    const Signal first(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(half));
    const auto a = renderMono(*e, s, first);
    std::copy(a.begin(), a.end(), out.begin());
    s.bypass = true;
    const Signal second(in.begin() + static_cast<std::ptrdiff_t>(half), in.end());
    const auto b = renderMono(*e, s, second);
    std::copy(b.begin(), b.end(), out.begin() + static_cast<std::ptrdiff_t>(half));

    for (size_t i = half + static_cast<size_t>(0.025 * sr); i < in.size(); ++i)
        REQUIRE(out[i] == in[i - static_cast<size_t>(latency)]);
    for (size_t i = 1; i < out.size(); ++i)
        REQUIRE(std::abs(out[i] - out[i - 1]) < 0.08f);

    // And back: the wet path is aligned with the bypass delay (no jump) once the crossfade is done.
    s.bypass = false;
    const auto c = renderMono(*e, s, in);
    double worst = 0.0;
    for (size_t i = 1; i < c.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(c[i] - c[i - 1])));
    CHECK(worst < 0.08);
}

TEST_CASE("Default engine stays flat with lookahead on", "[lookahead]")
{
    const double sr = 48000.0;
    const double freqs[] = {20, 63, 250, 1000, 4000, 8000, 12000, 18000};
    for (int bands : {3, 5})
        for (auto f : kFactors)
            for (float ms : {1.0f, 5.0f})
            {
                DYNAMIC_SECTION("bands " << bands << " x" << (1 << static_cast<int>(f)) << " lookahead "
                                         << ms)
                {
                    const auto s = neutral(f, ms, bands);
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

TEST_CASE("Mixes and band bypass stay aligned with lookahead on", "[lookahead]")
{
    const double sr = 48000.0;
    const double freqs[] = {30, 200, 1000, 5000, 15000};

    SECTION("global mix 0.5, neutral bands")
    {
        for (auto f : kFactors)
        {
            auto s = neutral(f, 5.0f);
            s.mix01 = 0.5f;
            auto e = makeEngine(sr, 512, 1, s);
            for (double hz : freqs)
            {
                const double g = steadyGainDb(*e, s, hz, sr);
                INFO("x" << (1 << static_cast<int>(f)) << " freq " << hz << " gain " << g << " dB");
                CHECK(std::abs(g) <= 0.1);
            }
        }
    }
    SECTION("per-band mix 0.5, neutral bands")
    {
        for (auto f : kFactors)
        {
            auto s = neutral(f, 5.0f);
            for (auto& b : s.bands)
                b.mix01 = 0.5f;
            auto e = makeEngine(sr, 512, 1, s);
            for (double hz : freqs)
            {
                const double g = steadyGainDb(*e, s, hz, sr);
                INFO("x" << (1 << static_cast<int>(f)) << " freq " << hz << " gain " << g << " dB");
                CHECK(std::abs(g) <= 0.1);
            }
        }
    }
    SECTION("one band bypassed behaves like a neutral band")
    {
        for (auto f : kFactors)
        {
            auto s = neutral(f, 5.0f);
            auto bp = s;
            bp.bands[1].bypass = true;
            bp.bands[1].attackDb = 12.0f; // ignored while bypassed
            bp.bands[1].sat = SatType::hardClip;
            const auto in = noise(16384, 5, 0.3f);
            auto e0 = makeEngine(sr, 512, 1, s);
            auto e1 = makeEngine(sr, 512, 1, bp);
            const auto a = renderMono(*e0, s, in);
            const auto b = renderMono(*e1, bp, in);
            const double res = residualDb(a, b, 0, 4096);
            INFO("x" << (1 << static_cast<int>(f)) << " residual " << res << " dB");
            CHECK(res < -100.0);
            for (double hz : freqs)
            {
                const double g = steadyGainDb(*e1, bp, hz, sr);
                CHECK(std::abs(g) <= 0.1);
            }
        }
    }
    SECTION("auto gain on neutral bands stays at 0 dB")
    {
        auto s = neutral(OSFactor::x2, 5.0f);
        s.autoGain = true;
        auto e = makeEngine(sr, 512, 1, s);
        const auto in = noise(static_cast<size_t>(sr) * 2, 11, 0.3f);
        renderMono(*e, s, in);
        CHECK(std::abs(e->meters().autoGainDb.load()) < 0.05f);
    }
    SECTION("global mix 0 equals the phase matched dry")
    {
        const auto in = noise(16384, 7, 0.3f);
        auto ref = neutral(OSFactor::x2, 5.0f, 4);
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
        const double res = residualDb(expected, out, 0, 4096);
        INFO("residual " << res << " dB");
        CHECK(res < -60.0);

        // The dry is the Off dry delayed by the lookahead.
        auto off = ref;
        off.lookaheadMs = 0.0f;
        auto eOff = makeEngine(sr, 512, 1, off);
        const auto dryOff = renderMono(*eOff, off, in);
        CHECK(residualDb(dryOff, out, static_cast<size_t>(lookaheadSamples(5.0f, sr)), 4096) < -60.0);
    }
}

TEST_CASE("Lookahead makes the shaper act on the first millisecond of an attack", "[lookahead]")
{
    // Attack boost: more gain in the first ms with lookahead; attack cut: more attenuation.
    for (auto os : {OSFactor::x1, OSFactor::x2})
    {
        const int f = 1 << static_cast<int>(os);
        const double boostOff = firstMsGainDb(os, 0.0f, 12.0f);
        const double cutOff = firstMsGainDb(os, 0.0f, -12.0f);
        for (float ms : kLookaheads)
        {
            const double boost = firstMsGainDb(os, ms, 12.0f);
            const double cut = firstMsGainDb(os, ms, -12.0f);
            WARN("x" << f << " lookahead " << ms << " ms: first-ms gain with attack +12 dB: " << boost
                     << " dB (Off " << boostOff << " dB); attack -12 dB: " << cut << " dB (Off " << cutOff
                     << " dB)");
            CHECK(boost > boostOff + 2.0);
            CHECK(cut < cutOff - 2.0);
        }
        // More lookahead never reacts later than less.
        CHECK(firstMsGainDb(os, 5.0f, 12.0f) >= firstMsGainDb(os, 1.0f, 12.0f) - 0.5);
    }
}

TEST_CASE("Lookahead time is the same at every oversampling factor", "[lookahead]")
{
    const double sr = 48000.0;
    const float ms = 2.0f;
    int component[3] = {};
    double gain[3] = {};
    for (size_t fi = 0; fi < 3; ++fi)
    {
        const auto off = neutral(kFactors[fi]);
        auto on = off;
        on.lookaheadMs = ms;
        component[fi] = makeEngine(sr, 512, 1, on)->getLatencySamples() -
                        makeEngine(sr, 512, 1, off)->getLatencySamples();
        gain[fi] = firstMsGainDb(kFactors[fi], ms, 12.0f);
        CHECK(component[fi] == lookaheadSamples(ms, sr));
    }
    CHECK(component[0] == component[1]);
    CHECK(component[1] == component[2]);
    WARN("2 ms lookahead, first-ms gain with attack +12 dB: x1 " << gain[0] << " dB, x2 " << gain[1]
                                                                 << " dB, x4 " << gain[2] << " dB");
    CHECK(std::abs(gain[0] - gain[2]) < 2.0);
}

TEST_CASE("Switching the lookahead back off restores the Off output", "[lookahead]")
{
    // Bit exactness of Off against the pre-feature engine was verified by hashing renders of the code before
    // and after this change; here the on -> off transition must leave nothing behind (a reconfigure resets
    // the core, whose filters keep a negligible residue, like any other reset).
    const double sr = 48000.0;
    EngineSettings s;
    s.os = OSFactor::x2;
    s.autoGain = true;
    for (size_t b = 0; b < s.bands.size(); ++b)
    {
        s.bands[b].attackDb = 6.0f * static_cast<float>(b % 3);
        s.bands[b].sustainDb = -3.0f * static_cast<float>(b % 2);
        s.bands[b].sat = static_cast<SatType>((b + 1) % 6);
        s.bands[b].mix01 = b == 1 ? 0.5f : 1.0f;
    }
    const Channels warm{noise(5000, 9), noise(5000, 10)};
    const Channels in{noise(20000, 3, 0.4f), noise(20000, 4, 0.4f)};

    auto reference = makeEngine(sr, 256, 2, s);
    render(*reference, s, warm, 256);
    reference->reset();
    const auto expected = render(*reference, s, in, 256);

    auto e = makeEngine(sr, 256, 2, s);
    auto on = s;
    on.lookaheadMs = 5.0f;
    render(*e, on, warm, 256);
    e->setSettings(s);
    e->reset();
    const auto out = render(*e, s, in, 256);
    CHECK(e->getLatencySamples() == reference->getLatencySamples());
    for (size_t c = 0; c < 2; ++c)
        CHECK(residualDb(expected[c], out[c], 0, 0) < -100.0);
}

TEST_CASE("Output with lookahead does not depend on the host block size", "[lookahead]")
{
    const double sr = 48000.0;
    auto s = neutral(OSFactor::x2, 5.0f);
    for (auto& b : s.bands)
        b.attackDb = 9.0f;
    const auto in = noise(12000, 21, 0.3f);
    auto e1 = makeEngine(sr, 512, 1, s);
    const auto ref = renderMono(*e1, s, in, 512);
    for (int block : {1, 7, 64, 200})
    {
        auto e = makeEngine(sr, 512, 1, s);
        const auto out = renderMono(*e, s, in, block);
        INFO("block " << block);
        CHECK(residualDb(ref, out, 0, 0) < -90.0);
    }
}

TEST_CASE("The plugin reports the lookahead in its latency and keeps it in the state", "[lookahead]")
{
    PentagritAudioProcessor a;
    CHECK(a.getAPVTS().getRawParameterValue(pid::lookahead)->load() == Approx(0.0f));
    CHECK_FALSE(a.getAPVTS().getParameter(pid::lookahead)->isAutomatable());
    a.setPlayConfigDetails(2, 2, 48000.0, 512);
    a.prepareToPlay(48000.0, 512);
    const int base = a.getLatencySamples();

    setChoice(a, pid::lookahead, 3);
    a.prepareToPlay(48000.0, 512);
    CHECK(a.getLatencySamples() == base + 240);

    juce::MemoryBlock block;
    a.getStateInformation(block);
    PentagritAudioProcessor b;
    b.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(b.getAPVTS().getRawParameterValue(pid::lookahead)->load() == Approx(3.0f));
    CHECK(ParamRefs(b.getAPVTS()).snapshot().lookaheadMs == Approx(5.0f));
}
