// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

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
    setValue(a, pid::lookahead, 2.0f);
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
    CHECK(raw(b, pid::lookahead) == Approx(2.0f));
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

TEST_CASE("Future-version state is rejected, old and version-less states load", "[state]")
{
    auto makeBlock = [](PentagritAudioProcessor& src, int version, bool withVersion)
    {
        juce::MemoryBlock block;
        src.getStateInformation(block);
        auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
        REQUIRE(xml != nullptr);
        if (withVersion)
            xml->setAttribute("stateVersion", version);
        else
            xml->removeAttribute("stateVersion");
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary(*xml, out);
        return out;
    };

    PentagritAudioProcessor src;
    setValue(src, pid::inGain, -6.0f);
    setValue(src, pid::numBands, 5.0f);

    SECTION("future version leaves parameters untouched")
    {
        const auto block = makeBlock(src, kStateVersion + 1, true);
        PentagritAudioProcessor p;
        setValue(p, pid::numBands, 4.0f);
        setValue(p, pid::inGain, 3.0f);
        p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        CHECK(raw(p, pid::numBands) == Approx(4.0f));
        CHECK(raw(p, pid::inGain) == Approx(3.0f).margin(0.05f));
    }
    SECTION("version 1 loads")
    {
        const auto block = makeBlock(src, 1, true);
        PentagritAudioProcessor p;
        p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        CHECK(raw(p, pid::numBands) == Approx(5.0f));
        CHECK(raw(p, pid::inGain) == Approx(-6.0f).margin(0.05f));
    }
    SECTION("missing version loads as version 1")
    {
        const auto block = makeBlock(src, 0, false);
        PentagritAudioProcessor p;
        p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        CHECK(raw(p, pid::numBands) == Approx(5.0f));
        CHECK(raw(p, pid::inGain) == Approx(-6.0f).margin(0.05f));
    }
}

TEST_CASE("State reproduces every parameter set without message-loop pumping", "[state]")
{
    // Mirrors the clap-validator state-reproducibility tests: parameters are changed the way a host
    // does (setValueNotifyingHost, no message loop in between), the state is saved, loaded into a fresh
    // instance and every value is compared; saving again must give the same bytes.
    PentagritAudioProcessor a;
    const auto& params = a.getParameters();
    REQUIRE(params.size() == kNumParameters);

    juce::Random rng(0x5EED);
    for (auto* p : params)
    {
        // Snap to the parameter's own grid (steps, choices, switches) like a host that sends valid values.
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
        REQUIRE(ranged != nullptr);
        const float v = ranged->convertTo0to1(ranged->convertFrom0to1(rng.nextFloat()));
        p->setValueNotifyingHost(v);
    }

    juce::MemoryBlock saved;
    a.getStateInformation(saved);
    REQUIRE(saved.getSize() > 0);

    PentagritAudioProcessor b;
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

    REQUIRE(b.getParameters().size() == params.size());
    for (int i = 0; i < params.size(); ++i)
    {
        auto* pa = params[i];
        auto* pb = b.getParameters()[i];
        INFO(pa->getName(64).toStdString() << " " << pa->getValue() << " vs " << pb->getValue());
        CHECK(pb->getValue() == Approx(pa->getValue()).margin(1e-6));
        const auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(pa);
        REQUIRE(withId != nullptr);
        CHECK(raw(b, withId->paramID) == Approx(raw(a, withId->paramID)).margin(1e-6));
    }

    juce::MemoryBlock again;
    b.getStateInformation(again);
    CHECK(again == saved);
}

TEST_CASE("State reproduces parameters set from the audio thread like the CLAP wrapper does", "[state]")
{
    PentagritAudioProcessor a;
    a.setPlayConfigDetails(2, 2, 48000.0, 512);
    a.prepareToPlay(48000.0, 512);
    const auto& params = a.getParameters();

    juce::Random rng(0xC1A9);
    std::vector<float> expected;
    for (auto* p : params)
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
        expected.push_back(ranged->convertTo0to1(ranged->convertFrom0to1(rng.nextFloat())));
    }

    std::thread audio(
        [&]
        {
            // Same calls as clap-juce-extensions::paramSetValueAndNotifyIfChanged, followed by one block.
            for (int i = 0; i < params.size(); ++i)
            {
                auto* p = params[i];
                if (p->getValue() == expected[static_cast<size_t>(i)])
                    continue;
                p->setValue(expected[static_cast<size_t>(i)]);
                p->sendValueChangedMessageToListeners(expected[static_cast<size_t>(i)]);
            }
            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            juce::MidiBuffer midi;
            a.processBlock(buffer, midi);
        });
    audio.join();

    juce::MemoryBlock saved;
    a.getStateInformation(saved);

    PentagritAudioProcessor b;
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    for (int i = 0; i < params.size(); ++i)
    {
        INFO(params[i]->getName(64).toStdString());
        CHECK(b.getParameters()[i]->getValue() == Approx(expected[static_cast<size_t>(i)]).margin(1e-6));
    }
}

TEST_CASE("Loading a state tells the host that the parameter values changed", "[state]")
{
    // A CLAP host caches parameter values and re-reads them only after a values rescan, which the wrapper
    // sends for a programChanged notification (clap-validator: state-reproducibility-*).
    struct Spy final : juce::AudioProcessorListener
    {
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
        {
            programChanged = programChanged || details.programChanged;
        }
        bool programChanged = false;
    };

    PentagritAudioProcessor a;
    setValue(a, pid::inGain, -6.0f);
    juce::MemoryBlock block;
    a.getStateInformation(block);

    PentagritAudioProcessor b;
    Spy spy;
    b.addListener(&spy);
    b.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(spy.programChanged);

    // A rejected state changes nothing and says nothing.
    spy.programChanged = false;
    const char junk[] = "not a state";
    b.setStateInformation(junk, static_cast<int>(sizeof(junk)));
    CHECK_FALSE(spy.programChanged);
    b.removeListener(&spy);
}
