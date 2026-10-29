// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "PluginInfo.h"

namespace pentagrit
{
namespace
{
const juce::Identifier kStateVersionProp{"stateVersion"};
const juce::Identifier kUiWidthProp{"uiWidth"};
const juce::Identifier kUiHeightProp{"uiHeight"};
} // namespace

PentagritAudioProcessor::PentagritAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, juce::Identifier(kStateTag), createParameterLayout()), params_(apvts_),
      presetManager_(apvts_)
{
}

const juce::String PentagritAudioProcessor::getName() const
{
    return info::kName;
}

void PentagritAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = sampleRate;
    engine_.prepare(sampleRate, samplesPerBlock, juce::jmax(1, getTotalNumOutputChannels()));
    engine_.setSettings(params_.snapshot());
    setLatencySamples(engine_.getLatencySamples());
}

void PentagritAudioProcessor::releaseResources() {}

bool PentagritAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void PentagritAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    engine_.setSettings(params_.snapshot());
    engine_.process(buffer);

    const int latency = engine_.getLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples(latency);

    // Smoothed fraction of the block time spent here (cheap, realtime safe).
    if (currentSampleRate_ > 0.0 && buffer.getNumSamples() > 0)
    {
        const double elapsed =
            juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - startTicks);
        const double budget = buffer.getNumSamples() / currentSampleRate_;
        auto& load = engine_.meters().cpuLoad;
        const float prev = load.load(std::memory_order_relaxed);
        load.store(prev + 0.1f * (static_cast<float>(elapsed / budget) - prev), std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* PentagritAudioProcessor::createEditor()
{
    return new PentagritAudioProcessorEditor(*this);
}

juce::AudioParameterBool* PentagritAudioProcessor::getBypassParameter() const
{
    return dynamic_cast<juce::AudioParameterBool*>(apvts_.getParameter(pid::bypass));
}

void PentagritAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    state.setProperty(kStateVersionProp, kStateVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void PentagritAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(kStateTag))
        return;

    auto tree = juce::ValueTree::fromXml(*xml);
    if (tree.isValid())
        apvts_.replaceState(tree);
}

juce::Point<int> PentagritAudioProcessor::getUiSize() const
{
    const auto& s = apvts_.state;
    return {juce::jlimit(200, 4000, static_cast<int>(s.getProperty(kUiWidthProp, kDefaultUiWidth))),
            juce::jlimit(200, 4000, static_cast<int>(s.getProperty(kUiHeightProp, kDefaultUiHeight)))};
}

void PentagritAudioProcessor::setUiSize(int width, int height)
{
    apvts_.state.setProperty(kUiWidthProp, width, nullptr);
    apvts_.state.setProperty(kUiHeightProp, height, nullptr);
}
} // namespace pentagrit
