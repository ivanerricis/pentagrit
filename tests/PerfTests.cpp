// SPDX-License-Identifier: AGPL-3.0-or-later
// Hidden CPU benchmarks ([.perf]); run with `PentagritTests "[.perf]"`. Every figure is the MINIMUM over
// several repetitions to defeat scheduling noise from other processes.
#include "TestSignals.h"
#include "dsp/AutoGain.h"
#include "dsp/Crossover.h"
#include "dsp/DspUtils.h"
#include "dsp/MultibandEngine.h"
#include "dsp/Saturator.h"
#include "dsp/TransientShaper.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>

using namespace pentagrit;
using namespace pentagrit::test;

namespace
{
double minSeconds(int reps, const std::function<void()>& fn)
{
    double best = std::numeric_limits<double>::max();
    for (int r = 0; r < reps; ++r)
    {
        const auto t0 = juce::Time::getHighResolutionTicks();
        fn();
        best = std::min(best,
                        juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0));
    }
    return best;
}

// Cost of processing `numBlocks` blocks of `block` samples at `rate`, as % of real time at that rate.
double percentRealTime(double secs, int numBlocks, int block, double rate)
{
    return 100.0 * secs / (static_cast<double>(numBlocks) * block / rate);
}

void fillNoise(juce::AudioBuffer<float>& b, float amp, unsigned seed)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const auto x = noise(static_cast<size_t>(b.getNumSamples()), seed + static_cast<unsigned>(c), amp);
        std::copy(x.begin(), x.end(), b.getWritePointer(c));
    }
}
} // namespace

TEST_CASE("Perf: engine per oversampling factor (5 bands, stereo, 48 kHz, block 512)", "[.perf]")
{
    const double sr = 48000.0;
    const int block = 512;
    const int numBlocks = static_cast<int>(sr * 3 / block);
    for (auto f : {OSFactor::x1, OSFactor::x2, OSFactor::x4, OSFactor::x8})
    {
        EngineSettings s;
        s.os = f;
        s.numBands = 5;
        for (auto& b : s.bands)
        {
            b.sat = SatType::tube;
            b.attackDb = 6.0f;
            b.sustainDb = -3.0f;
        }
        s.autoGain = true;
        MultibandEngine e;
        e.prepare(sr, block, 2);
        e.setSettings(s);
        juce::AudioBuffer<float> in(2, block), buf(2, block);
        fillNoise(in, 0.3f, 1);
        const double secs = minSeconds(5,
                                       [&]
                                       {
                                           for (int i = 0; i < numBlocks; ++i)
                                           {
                                               for (int c = 0; c < 2; ++c)
                                                   buf.copyFrom(c, 0, in, c, 0, block);
                                               e.setSettings(s);
                                               e.process(buf);
                                           }
                                       });
        std::printf("PERF engine x%d: %.3f ms/block  %.2f %% of one core\n", 1 << static_cast<int>(f),
                    1000.0 * secs / numBlocks, percentRealTime(secs, numBlocks, block, sr));
    }
}

TEST_CASE("Perf: modules at 384 kHz (block 4096, stereo)", "[.perf]")
{
    const double rate = 384000.0;
    const int block = 4096;
    const int numBlocks = 100;
    juce::AudioBuffer<float> in(2, block), buf(2, block);
    fillNoise(in, 0.3f, 7);
    auto reset = [&]
    {
        for (int c = 0; c < 2; ++c)
            buf.copyFrom(c, 0, in, c, 0, block);
    };
    auto report = [&](const std::string& name, double secs)
    {
        std::printf("PERF module %-22s %.2f %% of one core @384k  (%.1f ns/sample/ch)\n", name.c_str(),
                    percentRealTime(secs, numBlocks, block, rate), 1.0e9 * secs / (numBlocks * block * 2.0));
    };

    report("copy-only (baseline)", minSeconds(5,
                                              [&]
                                              {
                                                  for (int i = 0; i < numBlocks; ++i)
                                                      reset();
                                              }));
    {
        TransientShaper t;
        t.prepare(rate, block, 2);
        t.setParameters(6.0f, -3.0f, 0.5f);
        report("TransientShaper", minSeconds(5,
                                             [&]
                                             {
                                                 for (int i = 0; i < numBlocks; ++i)
                                                 {
                                                     reset();
                                                     t.process(juce::dsp::AudioBlock<float>(buf));
                                                 }
                                             }));
    }
    for (int n : {3, 5})
    {
        Crossover x;
        x.prepare(48000.0, rate, block, 2);
        x.setNumBands(n);
        juce::AudioBuffer<float> b[5];
        Crossover::BandBlocks blocks;
        for (int i = 0; i < n; ++i)
        {
            b[i].setSize(2, block);
            blocks[static_cast<size_t>(i)] = juce::dsp::AudioBlock<float>(b[i]);
        }
        report("Crossover split " + std::to_string(n) + "b",
               minSeconds(5,
                          [&]
                          {
                              for (int i = 0; i < numBlocks; ++i)
                                  x.split(juce::dsp::AudioBlock<const float>(in), blocks);
                          }));
        report("Crossover dryAP " + std::to_string(n) + "b",
               minSeconds(5,
                          [&]
                          {
                              for (int i = 0; i < numBlocks; ++i)
                              {
                                  reset();
                                  x.processAllpassChain(juce::dsp::AudioBlock<float>(buf));
                              }
                          }));
    }
    const std::pair<SatType, const char*> types[] = {{SatType::tanh, "Saturator tanh"},
                                                     {SatType::softClip, "Saturator softClip"},
                                                     {SatType::tube, "Saturator tube"},
                                                     {SatType::wavefold, "Saturator wavefold"},
                                                     {SatType::hardClip, "Saturator hardClip"}};
    for (auto& [ty, name] : types)
    {
        Saturator s;
        s.prepare(rate, block, 2);
        s.setType(ty);
        s.setDriveDb(6.0f);
        for (int i = 0; i < 4; ++i) // finish the type crossfade
            s.process(juce::dsp::AudioBlock<float>(buf));
        report(name, minSeconds(5,
                                [&]
                                {
                                    for (int i = 0; i < numBlocks; ++i)
                                    {
                                        reset();
                                        s.process(juce::dsp::AudioBlock<float>(buf));
                                    }
                                }));
    }
    {
        AutoGain a;
        a.prepare(rate, block, 2);
        a.setEnabled(true);
        juce::AudioBuffer<float> ref(2, block);
        fillNoise(ref, 0.3f, 9);
        report("AutoGain", minSeconds(5,
                                      [&]
                                      {
                                          for (int i = 0; i < numBlocks; ++i)
                                          {
                                              reset();
                                              a.process(juce::dsp::AudioBlock<const float>(ref),
                                                        juce::dsp::AudioBlock<float>(buf));
                                          }
                                      }));
    }
}

TEST_CASE("Perf: oversampler up+down per factor (block 512, stereo, 48 kHz base)", "[.perf]")
{
    const int block = 512;
    const int numBlocks = 500;
    for (int stages : {1, 2, 3})
    {
        for (bool fir : {true, false})
        {
            juce::dsp::Oversampling<float> os(
                2, static_cast<size_t>(stages),
                fir ? juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple
                    : juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                true, !fir);
            os.initProcessing(static_cast<size_t>(block));
            juce::AudioBuffer<float> in(2, block);
            fillNoise(in, 0.3f, 3);
            const double secs = minSeconds(5,
                                           [&]
                                           {
                                               for (int i = 0; i < numBlocks; ++i)
                                               {
                                                   juce::dsp::AudioBlock<float> b(in);
                                                   os.processSamplesUp(juce::dsp::AudioBlock<const float>(b));
                                                   os.processSamplesDown(b);
                                               }
                                           });
            std::printf("PERF oversampler x%d %s: %.2f %% of one core @48k base\n", 1 << stages,
                        fir ? "FIR" : "IIR", percentRealTime(secs, numBlocks, block, 48000.0));
        }
    }
}

// ---- accuracy of the fast approximations against the std reference (not hidden) ----

TEST_CASE("Fast dB conversions match std::pow / std::log10", "[approx]")
{
    double worstDb = 0.0, worstRel = 0.0;
    for (double db = -140.0; db <= 80.0; db += 0.0137)
    {
        const double ref = std::pow(10.0, db / 20.0);
        const double got = dbToGain(static_cast<float>(db));
        worstRel = std::max(worstRel, std::abs(got / ref - 1.0));
        worstDb = std::max(worstDb, std::abs(20.0 * std::log10(got / ref)));
    }
    INFO("max error " << worstDb << " dB, rel " << worstRel);
    CHECK(worstDb < 1.0e-3);
    CHECK(dbToGain(0.0f) == 1.0f);
    CHECK(dbToGain(-1.0e9f) == 0.0f);

    double worstBack = 0.0;
    for (double g = 1.0e-5; g < 1.0e4; g *= 1.0007)
        worstBack =
            std::max(worstBack, std::abs(static_cast<double>(gainToDb(static_cast<float>(g))) -
                                         20.0 * std::log10(static_cast<double>(static_cast<float>(g)))));
    INFO("gainToDb max error " << worstBack << " dB");
    CHECK(worstBack < 1.0e-3);
    CHECK(gainToDb(0.0f) == kSilenceDb);
    CHECK(gainToDb(1.0f) == 0.0f);
}

TEST_CASE("Fast tanh and sine approximations stay within their error bounds", "[approx]")
{
    double worstTanh = 0.0, worstSin = 0.0;
    for (double x = -60.0; x <= 60.0; x += 0.00137)
    {
        const auto xf = static_cast<float>(x);
        worstTanh = std::max(worstTanh, std::abs(static_cast<double>(fastTanh(xf)) - std::tanh(xf)));
        worstSin = std::max(worstSin, std::abs(static_cast<double>(fastSinHalfPi(xf)) -
                                               std::sin(1.57079632679489662 * static_cast<double>(xf))));
    }
    INFO("tanh " << worstTanh << " sin " << worstSin);
    CHECK(worstTanh < 1.0e-4);
    CHECK(worstSin < 1.0e-5);
    CHECK(fastTanh(0.0f) == 0.0f);
    CHECK(fastSinHalfPi(0.0f) == 0.0f);
    CHECK(std::isfinite(fastSinHalfPi(std::numeric_limits<float>::infinity())));
    // Relative accuracy for tiny signals (no noise floor from the approximation).
    CHECK(std::abs(fastTanh(1.0e-4f) / 1.0e-4f - 1.0f) < 1.0e-6f);
}

TEST_CASE("Saturator curves match the std::tanh / std::sin reference", "[approx]")
{
    const float tubeOffset = std::tanh(0.25f);
    double worst[4] = {0, 0, 0, 0};
    for (double u = -50.0; u <= 50.0; u += 0.00173)
    {
        const auto f = static_cast<float>(u);
        worst[0] = std::max(worst[0],
                            std::abs(static_cast<double>(Saturator::shape(SatType::tanh, f)) - std::tanh(f)));
        worst[1] = std::max(worst[1], std::abs(static_cast<double>(Saturator::shape(SatType::tube, f)) -
                                               (std::tanh(f + 0.25f) - tubeOffset)));
        worst[2] = std::max(worst[2], std::abs(static_cast<double>(Saturator::shape(SatType::wavefold, f)) -
                                               std::sin(1.57079632679489662 * f)));
    }
    INFO("tanh " << worst[0] << " tube " << worst[1] << " wavefold " << worst[2]);
    CHECK(worst[0] < 1.0e-4);
    CHECK(worst[1] < 2.0e-4);
    CHECK(worst[2] < 1.0e-5);
    CHECK(Saturator::shape(SatType::tube, 0.0f) == 0.0f);
}
