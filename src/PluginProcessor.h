// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/MultibandEngine.h"
#include "params/Parameters.h"
#include "presets/PresetManager.h"
#include "settings/SettingsManager.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace pentagrit
{
inline constexpr int kDefaultUiWidth = 900;
inline constexpr int kDefaultUiHeight = 600;

class PentagritAudioProcessor final : public juce::AudioProcessor,
                                      private juce::ChangeListener,
                                      private juce::Timer
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

    // Delta monitoring: the output is only what the plugin changed (wet minus the phase-matched dry). A
    // session flag of this instance: never saved in the project or in presets, off when the plugin loads.
    // Bypass wins. Thread safe.
    void setDeltaMonitoring(bool on) noexcept { deltaMonitoring_.store(on, std::memory_order_relaxed); }
    bool isDeltaMonitoring() const noexcept { return deltaMonitoring_.load(std::memory_order_relaxed); }
    // True when the per-user option "level matched bypass" is on (mirror of the settings, safe to read from
    // any thread). The engine only applies it while rendering in real time.
    bool isLevelMatchBypass() const noexcept { return levelMatchBypass_.load(std::memory_order_relaxed); }

    // Undo history of the changes made from the plugin UI (MESSAGE THREAD ONLY; the audio thread never
    // touches it). One transaction per UI gesture: a knob drag, a typed value, a toggle click, a crossover
    // handle drag or a selector change (with the crossover handles it moves). Host automation is not
    // recorded. Loading a preset, switching A/B and restoring a host state clear it.
    juce::UndoManager& getUndoManager() noexcept { return undoManager_; }
    void clearUndoHistory();
    // True while a UI gesture is in progress (between beginChangeGesture and endChangeGesture).
    bool isUiGestureOpen() const noexcept;
    // While alive, gestures are not recorded: for changes the editor makes as a side effect of a host change.
    class ScopedUndoPause
    {
    public:
        explicit ScopedUndoPause(PentagritAudioProcessor& p) : p_(p) { ++p_.undoPause_; }
        ~ScopedUndoPause() { --p_.undoPause_; }
        ScopedUndoPause(const ScopedUndoPause&) = delete;
        ScopedUndoPause& operator=(const ScopedUndoPause&) = delete;

    private:
        PentagritAudioProcessor& p_;
    };
    // History bound: about this many single-parameter steps are kept.
    static constexpr int kUndoMaxUnits = 200;
    static constexpr int kUndoMinTransactions = 50;

    // Editor size, persisted in the plugin state.
    juce::Point<int> getUiSize() const;
    void setUiSize(int width, int height);

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    // Message thread: forwards the engine latency (stored by the audio thread) to the host.
    void timerCallback() override;
    // Single place for state migrations; called with the version found in the saved state.
    static void migrateState(juce::ValueTree& state, int fromVersion);
    // Parameter snapshot plus the session / per-user monitoring flags (audio thread safe).
    EngineSettings engineSettings() const noexcept;

    class UndoRecorder;

    juce::UndoManager undoManager_{kUndoMaxUnits, kUndoMinTransactions}; // outlives apvts_ and the recorder
    juce::AudioProcessorValueTreeState apvts_;
    ParamRefs params_;
    MultibandEngine engine_;
    PresetManager presetManager_;
    juce::SharedResourcePointer<SettingsManager> settings_;
    double currentSampleRate_ = 0.0;
    std::atomic<int> pendingLatency_{0};
    std::atomic<bool> pendingUndoClear_{false};
    std::atomic<bool> deltaMonitoring_{false};
    std::atomic<bool> levelMatchBypass_{false};  // mirror of UserSettings::levelMatchBypass
    int undoPause_ = 0;                          // message thread
    std::unique_ptr<UndoRecorder> undoRecorder_; // last: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PentagritAudioProcessor)
};
} // namespace pentagrit
