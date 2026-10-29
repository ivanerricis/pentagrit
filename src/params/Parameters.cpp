// SPDX-License-Identifier: AGPL-3.0-or-later
#include "params/Parameters.h"

#include <cmath>

namespace pentagrit
{
juce::String bandParamID(int band0, BandParam p)
{
    static constexpr const char* names[] = {"gain",  "attack", "sustain", "speed", "satType",
                                            "drive", "mix",    "mute",    "solo",  "bypass"};
    return "b" + juce::String(band0 + 1) + "_" + names[static_cast<int>(p)];
}

namespace
{
using juce::ParameterID;
using Attr = juce::AudioParameterFloatAttributes;

ParameterID makeID(const juce::String& id)
{
    return ParameterID{id, kParamVersion};
}

juce::String dbText(float v, int)
{
    return juce::String(v, 1) + " dB";
}

juce::String percentText(float v, int)
{
    return juce::String(juce::roundToInt(v * 100.0f)) + " %";
}

juce::String hzText(float v, int)
{
    return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
}

// Logarithmic 20 Hz .. 20 kHz range.
juce::NormalisableRange<float> logFreqRange()
{
    constexpr float lo = 20.0f, hi = 20000.0f;
    return {lo, hi, [](float s, float e, float n) { return s * std::pow(e / s, n); },
            [](float s, float e, float v) { return std::log(juce::jlimit(s, e, v) / s) / std::log(e / s); },
            [](float s, float e, float v) { return juce::jlimit(s, e, v); }};
}

const juce::StringArray& satTypeNames()
{
    static const juce::StringArray names{"Off", "Tanh", "Soft Clip", "Tube", "Wavefold", "Hard Clip"};
    return names;
}

std::unique_ptr<juce::AudioParameterFloat> dbParam(const juce::String& id, const juce::String& name, float lo,
                                                   float hi, float def)
{
    return std::make_unique<juce::AudioParameterFloat>(
        makeID(id), name, juce::NormalisableRange<float>(lo, hi, 0.1f), def,
        Attr().withLabel("dB").withStringFromValueFunction(dbText));
}

std::unique_ptr<juce::AudioParameterFloat> unitParam(const juce::String& id, const juce::String& name,
                                                     float def)
{
    return std::make_unique<juce::AudioParameterFloat>(makeID(id), name,
                                                       juce::NormalisableRange<float>(0.0f, 1.0f), def,
                                                       Attr().withStringFromValueFunction(percentText));
}

std::unique_ptr<juce::AudioParameterBool> boolParam(const juce::String& id, const juce::String& name,
                                                    bool def)
{
    return std::make_unique<juce::AudioParameterBool>(makeID(id), name, def);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(dbParam(pid::inGain, "Input Gain", -24.0f, 24.0f, 0.0f));
    layout.add(dbParam(pid::outGain, "Output Gain", -24.0f, 24.0f, 0.0f));
    layout.add(unitParam(pid::mix, "Mix", 1.0f));
    layout.add(boolParam(pid::autoGain, "Auto Gain", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(makeID(pid::oversampling), "Oversampling",
                                                            juce::StringArray{"Off", "2x", "4x", "8x"}, 1));
    // Non-automatable, saved with the project.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        makeID(pid::osPhase), "OS Phase", juce::StringArray{"Linear", "Minimum"}, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        makeID(pid::osRender), "OS Render", juce::StringArray{"Same as live", "2x", "4x", "8x"}, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)));
    layout.add(std::make_unique<juce::AudioParameterInt>(makeID(pid::numBands), "Bands", 2, kMaxBands, 3));

    constexpr float xoverDefaults[kNumXovers] = {120.0f, 1200.0f, 3500.0f, 8000.0f};
    for (int i = 0; i < kNumXovers; ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            makeID(pid::xover[static_cast<size_t>(i)]), "Crossover " + juce::String(i + 1), logFreqRange(),
            xoverDefaults[i], Attr().withLabel("Hz").withStringFromValueFunction(hzText)));

    layout.add(boolParam(pid::bypass, "Bypass", false));

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto prefix = "Band " + juce::String(b + 1) + " ";
        auto id = [b](BandParam p) { return bandParamID(b, p); };

        layout.add(dbParam(id(BandParam::gain), prefix + "Gain", -24.0f, 24.0f, 0.0f));
        layout.add(dbParam(id(BandParam::attack), prefix + "Attack", -24.0f, 24.0f, 0.0f));
        layout.add(dbParam(id(BandParam::sustain), prefix + "Sustain", -24.0f, 24.0f, 0.0f));
        layout.add(unitParam(id(BandParam::speed), prefix + "Speed", 0.5f));
        layout.add(std::make_unique<juce::AudioParameterChoice>(makeID(id(BandParam::satType)),
                                                                prefix + "Saturation", satTypeNames(), 0));
        layout.add(dbParam(id(BandParam::drive), prefix + "Drive", 0.0f, 36.0f, 6.0f));
        layout.add(unitParam(id(BandParam::mix), prefix + "Mix", 1.0f));
        layout.add(boolParam(id(BandParam::mute), prefix + "Mute", false));
        layout.add(boolParam(id(BandParam::solo), prefix + "Solo", false));
        layout.add(boolParam(id(BandParam::bypass), prefix + "Bypass", false));
    }

    return layout;
}

ParamRefs::ParamRefs(juce::AudioProcessorValueTreeState& apvts)
{
    auto raw = [&apvts](const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue(id);
        jassert(p != nullptr);
        return p;
    };

    inGain = raw(pid::inGain);
    outGain = raw(pid::outGain);
    mix = raw(pid::mix);
    autoGain = raw(pid::autoGain);
    oversampling = raw(pid::oversampling);
    numBands = raw(pid::numBands);
    bypass = raw(pid::bypass);
    osPhase = raw(pid::osPhase);
    osRender = raw(pid::osRender);
    for (size_t i = 0; i < xover.size(); ++i)
        xover[i] = raw(pid::xover[i]);

    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& band = bands[static_cast<size_t>(b)];
        band.gain = raw(bandParamID(b, BandParam::gain));
        band.attack = raw(bandParamID(b, BandParam::attack));
        band.sustain = raw(bandParamID(b, BandParam::sustain));
        band.speed = raw(bandParamID(b, BandParam::speed));
        band.satType = raw(bandParamID(b, BandParam::satType));
        band.drive = raw(bandParamID(b, BandParam::drive));
        band.mix = raw(bandParamID(b, BandParam::mix));
        band.mute = raw(bandParamID(b, BandParam::mute));
        band.solo = raw(bandParamID(b, BandParam::solo));
        band.bypass = raw(bandParamID(b, BandParam::bypass));
    }
}

EngineSettings ParamRefs::snapshot() const noexcept
{
    auto ld = [](const std::atomic<float>* a) { return a->load(std::memory_order_relaxed); };
    auto on = [&ld](const std::atomic<float>* a) { return ld(a) >= 0.5f; };

    EngineSettings s;
    s.inGainDb = ld(inGain);
    s.outGainDb = ld(outGain);
    s.mix01 = ld(mix);
    s.autoGain = on(autoGain);
    s.bypass = on(bypass);
    s.os = static_cast<OSFactor>(juce::jlimit(0, 3, juce::roundToInt(ld(oversampling))));
    s.osPhase = ld(osPhase) >= 0.5f ? OSPhase::minimum : OSPhase::linear;
    {
        const int r = juce::jlimit(0, 3, juce::roundToInt(ld(osRender)));
        s.osRenderSame = (r == 0);
        s.osRender = static_cast<OSFactor>(r == 0 ? 0 : r); // 1..3 -> x2, x4, x8
    }
    s.numBands = juce::jlimit(2, kMaxBands, juce::roundToInt(ld(numBands)));
    for (size_t i = 0; i < xover.size(); ++i)
        s.xoverHz[i] = ld(xover[i]);

    for (size_t b = 0; b < bands.size(); ++b)
    {
        const auto& r = bands[b];
        auto& d = s.bands[b];
        d.gainDb = ld(r.gain);
        d.attackDb = ld(r.attack);
        d.sustainDb = ld(r.sustain);
        d.speed01 = ld(r.speed);
        d.sat = static_cast<SatType>(juce::jlimit(0, 5, juce::roundToInt(ld(r.satType))));
        d.driveDb = ld(r.drive);
        d.mix01 = ld(r.mix);
        d.mute = on(r.mute);
        d.solo = on(r.solo);
        d.bypass = on(r.bypass);
    }
    return s;
}
} // namespace pentagrit
