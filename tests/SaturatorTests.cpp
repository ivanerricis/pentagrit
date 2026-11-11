// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestSignals.h"
#include "dsp/DspUtils.h"
#include "dsp/Saturator.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>

using namespace pentagrit;
using namespace pentagrit::test;

namespace
{
// Measured at 1x, 44.1 kHz, 5 kHz sine: hard clip 7.9 dB (24 dB drive), wavefold 12.9 dB (24 dB drive).
constexpr double kMinAdaaReductionDb = 6.0;
constexpr SatType kAll[] = {SatType::tanh, SatType::softClip, SatType::tube, SatType::wavefold,
                            SatType::hardClip};

void run(Saturator& s, Signal& x)
{
    float* ch[1] = {x.data()};
    s.process(juce::dsp::AudioBlock<float>(ch, 1, x.size()));
}

double harmonicDb(const Signal& y, size_t from, size_t len, double f0, double fs, int harmonic)
{
    Signal seg(y.begin() + static_cast<std::ptrdiff_t>(from),
               y.begin() + static_cast<std::ptrdiff_t>(from + len));
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

// Gain of the curve for a vanishing signal at 0 dB drive.
double smallSignalSlopeDb(SatType t)
{
    switch (t)
    {
        case SatType::softClip:
            return 20.0 * std::log10(1.5);
        case SatType::tube:
            return 20.0 * std::log10(1.0 - std::tanh(0.25) * std::tanh(0.25));
        case SatType::wavefold:
            return 20.0 * std::log10(kPi / 2.0);
        default:
            return 0.0;
    }
}

// A saturator settled on `t` at the given drive (type crossfade finished, silence processed).
Saturator makeSettled(SatType t, float driveDb, double fs, bool comp = false, int numCh = 1)
{
    Saturator s;
    s.prepare(fs, 512, numCh);
    s.setDriveDb(driveDb);
    s.setDriveComp(comp);
    s.setType(t);
    Signal warm(static_cast<size_t>(fs * 0.1), 0.0f);
    run(s, warm);
    return s;
}

// Output of the settled saturator for a sine, minus the start-up part.
double sineGainDb(SatType t, float driveDb, bool comp, double freq, double fs, float amp)
{
    auto s = makeSettled(t, driveDb, fs, comp);
    const size_t n = static_cast<size_t>(fs);
    auto x = sine(n, freq, fs, amp);
    const auto in = x;
    run(s, x);
    return toDb(rms(x, n / 2, n)) - toDb(rms(in, n / 2, n));
}

// Energy outside DC, the fundamental (bin 3715 of 32768 at 44.1 kHz = 4999.8 Hz) and its legitimate 3rd
// harmonic (the signals of the aliasing tests are odd symmetric, so only odd harmonics exist).
double aliasEnergy(const Signal& x)
{
    const size_t n = x.size();
    Signal w(n);
    for (size_t i = 0; i < n; ++i)
        w[i] = x[i] * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n)));
    const auto spec = spectrum(w);
    const int k0 = 3715;
    double alias = 0.0;
    for (size_t k = 0; k < spec.size(); ++k)
    {
        const int ki = static_cast<int>(k);
        if (!(ki < 12 || std::abs(ki - k0) < 12 || std::abs(ki - 3 * k0) < 12))
            alias += std::norm(spec[k]);
    }
    return alias;
}

// Reference one-pole DC blocker (double precision) used by the low level accuracy test.
Signal dcBlock(const std::vector<double>& s, double fs)
{
    const double c = std::exp(-2.0 * kPi * 5.0 / fs);
    Signal y(s.size());
    double x1 = 0.0, y1 = 0.0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        y1 = s[i] - x1 + c * y1;
        x1 = s[i];
        y[i] = static_cast<float>(y1);
    }
    return y;
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
        CHECK(std::abs(Saturator::shape(SatType::softClip, 1.0f) -
                       Saturator::shape(SatType::softClip, 1.0001f)) < 1e-4f);
        CHECK(std::abs(Saturator::shape(SatType::softClip, -1.0f) -
                       Saturator::shape(SatType::softClip, -1.0001f)) < 1e-4f);
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
    for (auto from : {SatType::tanh, SatType::tube, SatType::hardClip, SatType::wavefold, SatType::off})
        for (auto to : {SatType::wavefold, SatType::hardClip, SatType::softClip, SatType::off, SatType::tanh})
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

// ---- drive compensation ----

TEST_CASE("Drive compensation keeps the level of a low-level sine", "[Saturator][DriveComp]")
{
    constexpr double fs = 48000.0;
    for (auto t : kAll)
        for (float drive : {0.0f, 6.0f, 12.0f, 24.0f})
        {
            const double off = sineGainDb(t, drive, false, 1000.0, fs, 0.001f);
            const double on = sineGainDb(t, drive, true, 1000.0, fs, 0.001f);
            // Compensation divides by the drive gain only, so what is left is the small-signal slope of the
            // curve itself (the level the curve has at 0 dB drive).
            const double slopeDb = smallSignalSlopeDb(t);
            INFO("type " << int(t) << " drive " << drive << " dB: off " << off << " dB, on " << on
                         << " dB, slope " << slopeDb << " dB");
            CHECK(std::abs(on - slopeDb) < 0.1);
            // Without compensation the level follows the drive (sanity check of the measurement).
            CHECK(std::abs(off - slopeDb - drive) < 0.1);
        }
}

TEST_CASE("Drive compensation never touches the Off curve", "[Saturator][DriveComp]")
{
    Saturator s = makeSettled(SatType::off, 24.0f, 48000.0, true, 2);
    auto a = noise(4096, 3);
    auto b = noise(4096, 4);
    const auto ra = a, rb = b;
    float* ch[2] = {a.data(), b.data()};
    s.process(juce::dsp::AudioBlock<float>(ch, 2, a.size()));
    CHECK(a == ra);
    CHECK(b == rb);
}

TEST_CASE("Drive compensation off reproduces the plain saturator exactly", "[Saturator][DriveComp]")
{
    constexpr double fs = 48000.0;
    const double dcCoeff = std::exp(-2.0 * kPi * 5.0 / fs);
    for (auto t : {SatType::tanh, SatType::softClip, SatType::tube})
        for (float drive : {0.0f, 9.0f, 30.0f})
        {
            auto s = makeSettled(t, drive, fs, false);
            auto x = noise(8192, 11, 0.7f);
            const auto in = x;
            run(s, x);

            // Independent reference: drive gain, static curve, 5 Hz DC blocker.
            const float g = dbToGain(drive);
            float x1 = 0.0f, y1 = 0.0f;
            const float c = static_cast<float>(dcCoeff);
            double worst = 0.0;
            for (size_t i = 0; i < in.size(); ++i)
            {
                const float sh = Saturator::shape(t, in[i] * g);
                const float y = sh - x1 + c * y1;
                x1 = sh;
                y1 = y;
                worst = std::max(worst, double(std::abs(y - x[i])));
            }
            INFO("type " << int(t) << " drive " << drive << " worst deviation " << worst);
            CHECK(worst == 0.0);
        }
}

TEST_CASE("Switching drive compensation off again restores the plain path exactly", "[Saturator][DriveComp]")
{
    constexpr double fs = 48000.0;
    for (auto t : kAll)
    {
        Saturator plain = makeSettled(t, 12.0f, fs, false, 2);
        Saturator toggled = makeSettled(t, 12.0f, fs, true, 2);
        toggled.setDriveComp(false);
        auto step = [](Saturator& s, unsigned seed)
        {
            auto a = noise(8192, seed, 0.5f), b = noise(8192, seed + 1, 0.5f);
            float* ch[2] = {a.data(), b.data()};
            s.process(juce::dsp::AudioBlock<float>(ch, 2, a.size()));
            return std::make_pair(a, b);
        };
        step(toggled, 1); // ramp (20 ms) and let the DC blocker forget the compensated history
        toggled.reset();  // state cleared; the comp ramp has settled
        plain.reset();
        const auto p = step(plain, 5), q = step(toggled, 5);
        INFO("type " << int(t));
        CHECK(p.first == q.first);
        CHECK(p.second == q.second);
    }
}

TEST_CASE("Toggling drive compensation is click free", "[Saturator][DriveComp]")
{
    constexpr double fs = 48000.0;
    for (auto t : kAll)
        for (bool startOn : {false, true})
        {
            auto s = makeSettled(t, 12.0f, fs, startOn);
            Signal settle = sine(24000, 100.0, fs, 0.5f);
            run(s, settle);

            // Reference: largest sample step of the steady state in both states (the toggled run moves
            // from one to the other, so its steps must not exceed the larger of the two).
            auto steadyJump = [&](bool comp)
            {
                auto r = makeSettled(t, 12.0f, fs, comp);
                Signal pre = sine(24000, 100.0, fs, 0.5f);
                run(r, pre);
                Signal v = sine(9600, 100.0, fs, 0.5f);
                run(r, v);
                float j = 0.0f;
                for (size_t i = 1; i < v.size(); ++i)
                    j = std::max(j, std::abs(v[i] - v[i - 1]));
                return j;
            };
            const float refJump = std::max(steadyJump(false), steadyJump(true));

            s.setDriveComp(!startOn);
            auto x = sine(9600, 100.0, fs, 0.5f);
            run(s, x);
            float maxJump = 0.0f;
            for (size_t i = 1; i < x.size(); ++i)
                maxJump = std::max(maxJump, std::abs(x[i] - x[i - 1]));
            INFO("type " << int(t) << " start " << (startOn ? "on" : "off") << " steady jump " << refJump
                         << " toggled jump " << maxJump);
            CHECK(maxJump <= refJump * 1.5f + 0.005f);
        }
}

TEST_CASE("Drive compensation follows the smoothed drive without zipper noise", "[Saturator][DriveComp]")
{
    constexpr double fs = 48000.0;
    // A small signal stays at its level while the drive is swept: the output must not step.
    Saturator s = makeSettled(SatType::tanh, 0.0f, fs, true);
    Signal x = sine(48000, 200.0, fs, 0.001f);
    const auto in = x;
    Signal out;
    for (size_t pos = 0; pos < x.size(); pos += 480)
    {
        s.setDriveDb(static_cast<float>(pos / 480) * 0.3f); // 0 -> 30 dB in 100 steps
        Signal blk(x.begin() + static_cast<std::ptrdiff_t>(pos),
                   x.begin() + static_cast<std::ptrdiff_t>(pos + 480));
        run(s, blk);
        out.insert(out.end(), blk.begin(), blk.end());
    }
    double worstRatioDb = 0.0;
    for (size_t pos = 4800; pos + 480 <= out.size(); pos += 480)
    {
        const double r = toDb(rms(out, pos, pos + 480)) - toDb(rms(in, pos, pos + 480));
        worstRatioDb = std::max(worstRatioDb, std::abs(r));
    }
    INFO("worst block level deviation " << worstRatioDb << " dB");
    CHECK(worstRatioDb < 0.1);
}

// ---- ADAA of hard clip and wavefold ----

TEST_CASE("ADAA hard clip and wavefold alias less than the naive curve at 1x", "[Saturator][ADAA]")
{
    constexpr double fs = 44100.0;
    constexpr size_t n = 32768, warm = 16384;
    const double f0 = 3715.0 * fs / static_cast<double>(n);
    for (auto t : {SatType::hardClip, SatType::wavefold})
        for (float drive : {12.0f, 24.0f})
        {
            const auto in = sine(n + warm, f0, fs, 0.5f);

            // Naive: the static curve straight on the drive-scaled signal.
            const float g = dbToGain(drive);
            Signal naive(in.size());
            for (size_t i = 0; i < in.size(); ++i)
                naive[i] = Saturator::shape(t, in[i] * g);

            auto s = makeSettled(t, drive, fs);
            auto adaa = in;
            run(s, adaa);

            auto tail = [&](const Signal& v)
            { return Signal(v.begin() + static_cast<std::ptrdiff_t>(warm), v.end()); };
            const double aNaive = aliasEnergy(tail(naive)), aAdaa = aliasEnergy(tail(adaa));
            const double reductionDb = 10.0 * std::log10(aNaive / std::max(aAdaa, 1.0e-30));
            INFO("type " << int(t) << " drive " << drive << " alias reduction " << reductionDb << " dB");
            CHECK(reductionDb >= kMinAdaaReductionDb);
        }
}

TEST_CASE("ADAA loses some high end, as documented", "[Saturator][ADAA]")
{
    // Two-tap average: |H| = cos(pi f / fs). Low level and 0 dB drive keep the curves in their linear part.
    // Hard clip has unit slope; the wavefold slope pi/2 is divided out.
    struct Case
    {
        double rate;
        double expectedDb;
    };
    for (auto c : {Case{48000.0, -2.0}, Case{96000.0, -0.47}})
        for (auto t : {SatType::hardClip, SatType::wavefold})
        {
            const double slopeDb = t == SatType::wavefold ? 20.0 * std::log10(kPi / 2.0) : 0.0;
            const double g = sineGainDb(t, 0.0f, false, 10000.0, c.rate, 0.01f) - slopeDb;
            INFO("type " << int(t) << " rate " << c.rate << " gain at 10 kHz " << g << " dB");
            CHECK(g == Catch::Approx(c.expectedDb).margin(0.1));
            // Essentially no loss in the low and mid range.
            CHECK(std::abs(sineGainDb(t, 0.0f, false, 1000.0, c.rate, 0.01f) - slopeDb) < 0.05);
        }
}

TEST_CASE("ADAA is accurate for low-level signals", "[Saturator][ADAA]")
{
    // In the linear part ADAA1 is the two-tap average of the drive-scaled input times the slope of the
    // curve. A cancellation error in F(a) - F(b) would show up as noise here.
    constexpr double fs = 48000.0;
    for (auto t : {SatType::hardClip, SatType::wavefold})
        for (float amp : {1.0e-5f, 1.0e-3f, 0.05f})
        {
            auto s = makeSettled(t, 0.0f, fs);
            const auto in = sine(24000, 3000.0, fs, amp);
            auto out = in;
            run(s, out);

            const double slope = t == SatType::wavefold ? kPi / 2.0 : 1.0;
            std::vector<double> ref(in.size());
            for (size_t i = 0; i < in.size(); ++i)
                ref[i] = slope * 0.5 * (double(in[i]) + double(i ? in[i - 1] : 0.0f));
            const auto expected = dcBlock(ref, fs);

            double worst = 0.0;
            for (size_t i = 1; i < in.size(); ++i)
                worst = std::max(worst, std::abs(double(out[i]) - double(expected[i])));
            INFO("type " << int(t) << " amp " << amp << " worst error " << worst);
            // 0.05 amplitude: the cubic term of the sine curve adds a few 1e-4 relative error.
            CHECK(worst <= double(amp) * (amp > 0.01f ? 2.0e-2 : 1.0e-3));
        }
}

TEST_CASE("ADAA hard clip stays bounded for large steps", "[Saturator][ADAA]")
{
    // Alternating blocks of +-3 at drive 0 dB: the output stays within the curve range.
    Saturator s = makeSettled(SatType::hardClip, 0.0f, 48000.0);
    Signal x(4096);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = (i / 8) % 2 ? -3.0f : 3.0f;
    run(s, x);
    for (float v : x)
    {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::abs(v) <= 2.05f); // |y| <= 1, plus the DC blocker's overshoot
    }
}

TEST_CASE("ADAA keeps DC blocked", "[Saturator][ADAA]")
{
    constexpr double fs = 48000.0;
    for (auto t : {SatType::hardClip, SatType::wavefold})
    {
        auto s = makeSettled(t, 12.0f, fs);
        Signal x(96000, 0.3f);
        run(s, x);
        double tail = 0.0;
        for (size_t i = x.size() - 4800; i < x.size(); ++i)
            tail = std::max(tail, double(std::abs(x[i])));
        INFO("type " << int(t) << " residual DC " << tail);
        CHECK(tail < 1.0e-3);
    }
}

TEST_CASE("ADAA curves stay finite for silence, denormals and huge inputs", "[Saturator][ADAA]")
{
    const float inf = std::numeric_limits<float>::infinity();
    const float big = std::numeric_limits<float>::max();
    for (auto t : {SatType::hardClip, SatType::wavefold})
        for (double fs : {44100.0, 192000.0})
        {
            auto s = makeSettled(t, 36.0f, fs);
            Signal x;
            x.insert(x.end(), 2048, 0.0f);
            x.insert(x.end(), 2048, 1.0e-40f);
            x.insert(x.end(), 2048, -1.0e-40f);
            for (int i = 0; i < 2048; ++i)
                x.push_back(i % 2 ? 1.0e30f : -1.0e30f);
            for (int i = 0; i < 64; ++i)
                x.push_back(i % 3 == 0 ? inf : (i % 3 == 1 ? -inf : big));
            x.insert(x.end(), 4096, 0.0f);
            const auto big6 = noise(4096, 5, 1.0e6f);
            x.insert(x.end(), big6.begin(), big6.end());
            x.insert(x.end(), 8192, 0.0f);
            run(s, x);
            for (size_t i = 0; i < x.size(); ++i)
                REQUIRE(std::isfinite(x[i]));
        }
}

TEST_CASE("Saturator ADAA state is cleared by reset", "[Saturator][ADAA]")
{
    for (auto t : {SatType::hardClip, SatType::wavefold})
    {
        auto a = makeSettled(t, 12.0f, 48000.0, false, 2);
        auto b = makeSettled(t, 12.0f, 48000.0, false, 2);
        Signal loud = noise(1000, 2, 1.0f);
        Signal loud2 = loud;
        float* c0[2] = {loud.data(), loud2.data()};
        b.process(juce::dsp::AudioBlock<float>(c0, 2, loud.size()));
        b.reset();

        auto x1 = noise(2048, 9, 0.5f), x2 = noise(2048, 10, 0.5f);
        auto y1 = x1, y2 = x2;
        float* ca[2] = {x1.data(), x2.data()};
        float* cb[2] = {y1.data(), y2.data()};
        a.process(juce::dsp::AudioBlock<float>(ca, 2, x1.size()));
        b.process(juce::dsp::AudioBlock<float>(cb, 2, y1.size()));
        CHECK(x1 == y1);
        CHECK(x2 == y2);
    }
}
