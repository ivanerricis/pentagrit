// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pentagrit;
using Catch::Approx;

namespace
{
void setValue(PentagritAudioProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getAPVTS().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

float raw(PentagritAudioProcessor& p, const juce::String& id)
{
    return p.getAPVTS().getRawParameterValue(id)->load();
}
} // namespace

TEST_CASE("State round trip", "[state]")
{
    PentagritAudioProcessor a;
    setValue(a, pid::numBands, 3.0f);
    setValue(a, pid::inGain, -6.5f);
    setValue(a, pid::xover[1], 750.0f);
    setValue(a, bandParamID(2, BandParam::satType), 4.0f);
    setValue(a, bandParamID(2, BandParam::mute), 1.0f);
    a.setUiSize(1000, 640);

    juce::MemoryBlock block;
    a.getStateInformation(block);
    REQUIRE(block.getSize() > 0);

    auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
    REQUIRE(xml != nullptr);
    CHECK(xml->hasTagName("PENTAGRIT_STATE"));
    CHECK(xml->getIntAttribute("stateVersion") == 1);

    PentagritAudioProcessor b;
    b.setStateInformation(block.getData(), static_cast<int>(block.getSize()));

    CHECK(raw(b, pid::numBands) == Approx(3.0f));
    CHECK(raw(b, pid::inGain) == Approx(-6.5f).margin(0.05f));
    CHECK(raw(b, pid::xover[1]) == Approx(750.0f).epsilon(1e-3));
    CHECK(raw(b, bandParamID(2, BandParam::satType)) == Approx(4.0f));
    CHECK(raw(b, bandParamID(2, BandParam::mute)) == Approx(1.0f));
    CHECK(b.getUiSize().x == 1000);
    CHECK(b.getUiSize().y == 640);
}

TEST_CASE("Garbage state is ignored", "[state]")
{
    PentagritAudioProcessor p;
    setValue(p, pid::numBands, 4.0f);

    SECTION("random bytes")
    {
        const char junk[] = "this is definitely not a plugin state";
        p.setStateInformation(junk, static_cast<int>(sizeof(junk)));
    }
    SECTION("null and empty")
    {
        p.setStateInformation(nullptr, 0);
        const char dummy = 0;
        p.setStateInformation(&dummy, 0);
    }
    SECTION("valid XML with the wrong tag")
    {
        juce::XmlElement wrong("SOMETHING_ELSE");
        wrong.setAttribute("numBands", 2);
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary(wrong, mb);
        p.setStateInformation(mb.getData(), static_cast<int>(mb.getSize()));
    }

    CHECK(raw(p, pid::numBands) == Approx(4.0f));
}
