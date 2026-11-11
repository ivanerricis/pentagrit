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
    // The user preset folder and the startup preset come from the per-user settings. A host that restores a
    // saved state calls setStateInformation() afterwards, which replaces the whole state (including the
    // preset), so the startup preset never overrides a restored session.
    const auto& s = settings_->get();
    if (s.presetFolder.isNotEmpty())
        presetManager_.setUserFolder(settings_->getEffectivePresetFolder());
    SettingsManager::applyStartupPreset(s, presetManager_);

    settings_->addChangeListener(this);
    presetManager_.addChangeListener(this);
    startTimerHz(30);
}

PentagritAudioProcessor::~PentagritAudioProcessor()
{
    stopTimer();
    presetManager_.removeChangeListener(this);
    settings_->removeChangeListener(this);
}

void PentagritAudioProcessor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &presetManager_)
    {
        settings_->setLastPreset(SettingsManager::toRef(presetManager_.getCurrentEntry()));
    }
    else if (source == &*settings_)
    {
        const auto folder = settings_->getEffectivePresetFolder();
        if (folder != presetManager_.getUserFolder())
            presetManager_.setUserFolder(folder);
    }
}

const juce::String PentagritAudioProcessor::getName() const
{
    return info::kName;
}

void PentagritAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = sampleRate;
    engine_.setNonRealtime(isNonRealtime());
    engine_.prepare(sampleRate, samplesPerBlock, juce::jmax(1, getTotalNumOutputChannels()));
    engine_.setSettings(params_.snapshot());
    pendingLatency_.store(engine_.getLatencySamples(), std::memory_order_relaxed);
    setLatencySamples(engine_.getLatencySamples());
}

void PentagritAudioProcessor::timerCallback()
{
    const int latency = pendingLatency_.load(std::memory_order_relaxed);
    if (latency != getLatencySamples())
        setLatencySamples(latency);
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

    // Settings (incl. the live/render oversampling factor) are applied before the block; a factor, phase or
    // band count change reconfigures the engine and changes the latency, which the message-thread timer
    // reports.
    engine_.setNonRealtime(isNonRealtime());
    engine_.setSettings(params_.snapshot());
    engine_.process(buffer);

    pendingLatency_.store(engine_.getLatencySamples(), std::memory_order_relaxed);

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
    if (!tree.isValid())
        return;

    // A missing version means version 1. A state written by a newer plugin is not loaded.
    const int version = static_cast<int>(tree.getProperty(kStateVersionProp, 1));
    if (version > kStateVersion)
        return;

    migrateState(tree, version);
    apvts_.replaceState(tree);
}

void PentagritAudioProcessor::migrateState(juce::ValueTree&, int)
{
    // Place for future migrations: when kStateVersion is bumped, upgrade the tree here step by step
    // (if (fromVersion < 2) { ... }). Version 1 is the current layout, so nothing to do yet.
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
