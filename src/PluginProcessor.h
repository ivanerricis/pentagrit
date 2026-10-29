// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/MultibandEngine.h"
#include "params/Parameters.h"
#include "presets/PresetManager.h"
#include "settings/SettingsManager.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace pentagrit
{
inline constexpr int kDefaultUiWidth = 900;
inline constexpr int kDefaultUiHeight = 600;

class PentagritAudioProcessor final : public juce::AudioProcessor, private juce::ChangeListener
{
public:
    PentagritAudioProcessor();
    ~PentagritAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioParameterBool* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    MeterState& getMeters() noexcept { return engine_.meters(); }
    PresetManager& getPresetManager() noexcept { return presetManager_; }
    // Per-user settings, shared by all instances of the process.
    SettingsManager& getSettings() noexcept { return *settings_; }

    // Editor size, persisted in the plugin state.
    juce::Point<int> getUiSize() const;
    void setUiSize(int width, int height);

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::AudioProcessorValueTreeState apvts_;
    ParamRefs params_;
    MultibandEngine engine_;
    PresetManager presetManager_;
    juce::SharedResourcePointer<SettingsManager> settings_;
    double currentSampleRate_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PentagritAudioProcessor)
};
} // namespace pentagrit
