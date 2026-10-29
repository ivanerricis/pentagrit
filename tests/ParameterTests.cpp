// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <set>

using namespace pentagrit;
using Catch::Approx;

namespace
{
float raw(PentagritAudioProcessor& p, const juce::String& id)
{
    return p.getAPVTS().getRawParameterValue(id)->load();
}
} // namespace

TEST_CASE("Parameter layout has 63 unique, versioned IDs", "[parameters]")
{
    PentagritAudioProcessor proc;
    const auto& params = proc.getParameters();
    REQUIRE(static_cast<int>(params.size()) == kNumParameters);
    REQUIRE(kNumParameters == 63);

    std::set<juce::String> ids;
    for (auto* p : params)
    {
        auto* withID = dynamic_cast<juce::HostedAudioProcessorParameter*>(p);
        REQUIRE(withID != nullptr);
        ids.insert(withID->getParameterID());
        CHECK(withID->getVersionHint() == kParamVersion);
    }
    CHECK(ids.size() == 63);
}

TEST_CASE("Band parameter IDs follow the naming scheme", "[parameters]")
{
    CHECK(bandParamID(0, BandParam::gain) == "b1_gain");
    CHECK(bandParamID(4, BandParam::bypass) == "b5_bypass");

    PentagritAudioProcessor proc;
    for (int b = 0; b < kMaxBands; ++b)
        for (int p = 0; p < kNumBandParams; ++p)
            CHECK(proc.getAPVTS().getParameter(bandParamID(b, static_cast<BandParam>(p))) != nullptr);
}

TEST_CASE("Parameter defaults", "[parameters]")
{
    PentagritAudioProcessor proc;
    CHECK(raw(proc, pid::inGain) == Approx(0.0f));
    CHECK(raw(proc, pid::outGain) == Approx(0.0f));
    CHECK(raw(proc, pid::mix) == Approx(1.0f));
    CHECK(raw(proc, pid::autoGain) == Approx(0.0f));
    CHECK(raw(proc, pid::oversampling) == Approx(1.0f));
    CHECK(raw(proc, pid::numBands) == Approx(3.0f));
    CHECK(raw(proc, pid::bypass) == Approx(0.0f));
    CHECK(raw(proc, pid::osPhase) == Approx(0.0f));
    CHECK(raw(proc, pid::osRender) == Approx(0.0f));
    CHECK_FALSE(proc.getAPVTS().getParameter(pid::osPhase)->isAutomatable());
    CHECK_FALSE(proc.getAPVTS().getParameter(pid::osRender)->isAutomatable());

    const float xover[4] = {120.0f, 1200.0f, 3500.0f, 8000.0f};
    for (size_t i = 0; i < 4; ++i)
        CHECK(raw(proc, pid::xover[i]) == Approx(xover[i]).epsilon(1e-4));

    for (int b = 0; b < kMaxBands; ++b)
    {
        CHECK(raw(proc, bandParamID(b, BandParam::gain)) == Approx(0.0f));
        CHECK(raw(proc, bandParamID(b, BandParam::speed)) == Approx(0.5f));
        CHECK(raw(proc, bandParamID(b, BandParam::satType)) == Approx(0.0f));
        CHECK(raw(proc, bandParamID(b, BandParam::drive)) == Approx(6.0f));
        CHECK(raw(proc, bandParamID(b, BandParam::mix)) == Approx(1.0f));
        CHECK(raw(proc, bandParamID(b, BandParam::mute)) == Approx(0.0f));
    }
}

TEST_CASE("Parameter ranges and log-mapped crossover", "[parameters]")
{
    PentagritAudioProcessor proc;
    auto& apvts = proc.getAPVTS();

    for (auto* id : pid::xover)
    {
        const auto range = apvts.getParameterRange(id);
        CHECK(range.start == Approx(20.0f));
        CHECK(range.end == Approx(20000.0f));
        CHECK(range.convertFrom0to1(0.5f) == Approx(std::sqrt(20.0f * 20000.0f)).epsilon(1e-3));
        CHECK(range.convertTo0to1(632.455f) == Approx(0.5f).margin(1e-3));
    }

    CHECK(apvts.getParameterRange(pid::inGain).start == Approx(-24.0f));
    CHECK(apvts.getParameterRange(pid::inGain).end == Approx(24.0f));
    CHECK(apvts.getParameterRange(bandParamID(0, BandParam::drive)).end == Approx(36.0f));
    CHECK(apvts.getParameterRange(pid::numBands).start == Approx(2.0f));
    CHECK(apvts.getParameterRange(pid::numBands).end == Approx(5.0f));
}

TEST_CASE("snapshot() reflects parameter values", "[parameters]")
{
    PentagritAudioProcessor proc;
    auto& apvts = proc.getAPVTS();

    auto set = [&apvts](const juce::String& id, float value)
    {
        auto* p = apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    set(pid::numBands, 3.0f);
    set(pid::oversampling, 2.0f);
    set(pid::osPhase, 1.0f);
    set(pid::osRender, 3.0f);
    set(bandParamID(1, BandParam::satType), 3.0f);
    set(bandParamID(1, BandParam::solo), 1.0f);

    ParamRefs refs(apvts);
    const auto s = refs.snapshot();
    CHECK(s.numBands == 3);
    CHECK(s.os == OSFactor::x4);
    CHECK(s.osPhase == OSPhase::minimum);
    CHECK_FALSE(s.osRenderSame);
    CHECK(s.osRender == OSFactor::x8);
    CHECK(s.bands[1].sat == SatType::tube);
    CHECK(s.bands[1].solo);
    CHECK_FALSE(s.bands[0].solo);
}

TEST_CASE("snapshot() maps the render oversampling choice", "[parameters]")
{
    PentagritAudioProcessor proc;
    auto& apvts = proc.getAPVTS();
    ParamRefs refs(apvts);

    auto setChoice = [&apvts](const char* id, float value)
    {
        auto* p = apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };

    auto s = refs.snapshot();
    CHECK(s.osRenderSame);
    CHECK(s.osPhase == OSPhase::linear);
    CHECK(s.os == OSFactor::x2);
    CHECK(s.numBands == 3);

    setChoice(pid::osRender, 1.0f);
    s = refs.snapshot();
    CHECK_FALSE(s.osRenderSame);
    CHECK(s.osRender == OSFactor::x2);

    setChoice(pid::osRender, 2.0f);
    CHECK(refs.snapshot().osRender == OSFactor::x4);
}
