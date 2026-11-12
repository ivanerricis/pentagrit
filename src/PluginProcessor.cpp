// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "PluginInfo.h"

#include <vector>

namespace pentagrit
{
namespace
{
const juce::Identifier kStateVersionProp{"stateVersion"};
const juce::Identifier kUiWidthProp{"uiWidth"};
const juce::Identifier kUiHeightProp{"uiHeight"};

// One recorded parameter change. perform() runs when the step is recorded (the parameter already has the new
// value, so it does nothing) and on redo; undo() restores the old value. Values are normalised.
class ParameterChangeAction final : public juce::UndoableAction
{
public:
    ParameterChangeAction(juce::AudioProcessorParameter& p, float oldValue, float newValue)
        : param_(p), old_(oldValue), new_(newValue)
    {
    }
    bool perform() override { return apply(new_); }
    bool undo() override { return apply(old_); }
    int getSizeInUnits() override { return 1; }

private:
    bool apply(float value)
    {
        if (!juce::approximatelyEqual(param_.getValue(), value))
        {
            // A gesture, so that the host sees the change like any other edit made from the UI.
            param_.beginChangeGesture();
            param_.setValueNotifyingHost(value);
            param_.endChangeGesture();
        }
        return true;
    }

    juce::AudioProcessorParameter& param_;
    float old_, new_;
};
} // namespace

// Records the parameter gestures of the plugin UI as undo steps.
//
// Why not the UndoManager of the AudioProcessorValueTreeState? The APVTS copies parameter values into its
// tree from a timer (every 20 to 500 ms) and records every copy in its UndoManager, whatever caused the
// change: host automation would flood the history, and a UI change could not be told apart from automation at
// that point. Instead every UI edit already runs inside beginChangeGesture()/endChangeGesture() (the JUCE
// attachments do that, so do the crossover handles, the selectors and the typed values), while the host never
// calls them on our listeners. A parameter listener therefore sees exactly the UI edits: the value at the
// start and at the end of a gesture become one step. Gestures nested in another one (the band selector moving
// crossover handles) join the step of the outermost gesture.
class PentagritAudioProcessor::UndoRecorder final : private juce::AudioProcessorParameter::Listener
{
public:
    UndoRecorder(PentagritAudioProcessor& processor, juce::UndoManager& um) : processor_(processor), um_(um)
    {
        const auto n = static_cast<size_t>(processor_.getParameters().size());
        start_.assign(n, 0.0f);
        open_.assign(n, false);
        for (auto* p : processor_.getParameters())
        {
            p->addListener(this);
        }
    }

    ~UndoRecorder() override
    {
        for (auto* p : processor_.getParameters())
        {
            p->removeListener(this);
        }
    }

    bool isGestureOpen() const noexcept { return depth_ > 0; }

private:
    void parameterValueChanged(int, float) override {}

    void parameterGestureChanged(int index, bool starting) override
    {
        // Message thread only; undo/redo and the state restore do not record themselves.
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread() || um_.isPerformingUndoRedo() ||
            index < 0 || static_cast<size_t>(index) >= open_.size())
        {
            return;
        }
        const auto i = static_cast<size_t>(index);
        auto* param = processor_.getParameters()[index];

        if (starting)
        {
            if (open_[i] || processor_.undoPause_ > 0)
            {
                return;
            }
            if (depth_ == 0)
            {
                um_.beginNewTransaction(param->getName(64));
            }
            open_[i] = true;
            start_[i] = param->getValue();
            ++depth_;
        }
        else
        {
            if (!open_[i])
            {
                return;
            }
            open_[i] = false;
            --depth_;
            const float now = param->getValue();
            if (!juce::approximatelyEqual(start_[i], now))
            {
                um_.perform(new ParameterChangeAction(*param, start_[i], now));
            }
        }
    }

    PentagritAudioProcessor& processor_;
    juce::UndoManager& um_;
    std::vector<float> start_;
    std::vector<bool> open_;
    int depth_ = 0;
};

PentagritAudioProcessor::PentagritAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, juce::Identifier(kStateTag), createParameterLayout()), params_(apvts_),
      presetManager_(apvts_)
{
    // The APVTS gets no UndoManager on purpose, see UndoRecorder.
    undoRecorder_ = std::make_unique<UndoRecorder>(*this, undoManager_);
    presetManager_.setOnStateReplaced(
        [this] { clearUndoHistory(); }); // presets and A/B replace the state: history cleared

    // The user preset folder and the startup preset come from the per-user settings. A host that restores a
    // saved state calls setStateInformation() afterwards, which replaces the whole state (including the
    // preset), so the startup preset never overrides a restored session.
    const auto& s = settings_->get();
    if (s.presetFolder.isNotEmpty())
    {
        presetManager_.setUserFolder(settings_->getEffectivePresetFolder());
    }
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

void PentagritAudioProcessor::clearUndoHistory()
{
    // Some hosts restore the state from another thread: the UndoManager is only touched on the message thread
    // (the timer below picks the request up).
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        undoManager_.clearUndoHistory();
    }
    else
    {
        pendingUndoClear_.store(true, std::memory_order_release);
    }
}

bool PentagritAudioProcessor::isUiGestureOpen() const noexcept
{
    return undoRecorder_ != nullptr && undoRecorder_->isGestureOpen();
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
        {
            presetManager_.setUserFolder(folder);
        }
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
    if (pendingUndoClear_.exchange(false, std::memory_order_acq_rel))
    {
        undoManager_.clearUndoHistory();
    }
    const int latency = pendingLatency_.load(std::memory_order_relaxed);
    if (latency != getLatencySamples())
    {
        setLatencySamples(latency);
    }
}

void PentagritAudioProcessor::releaseResources() {}

bool PentagritAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
    {
        return false;
    }
    return out == layouts.getMainInputChannelSet();
}

void PentagritAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
    {
        buffer.clear(ch, 0, buffer.getNumSamples());
    }

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
    {
        copyXmlToBinary(*xml, destData);
    }
}

void PentagritAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
    {
        return;
    }

    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(kStateTag))
    {
        return;
    }

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid())
    {
        return;
    }

    // A missing version means version 1. A state written by a newer plugin is not loaded.
    const int version = static_cast<int>(tree.getProperty(kStateVersionProp, 1));
    if (version > kStateVersion)
    {
        return;
    }

    migrateState(tree, version);
    apvts_.replaceState(tree);
    clearUndoHistory(); // the whole state was replaced: older steps no longer apply

    // Tell the host that every parameter value changed. A CLAP host caches the values and only re-reads them
    // after a params rescan (CLAP_PARAM_RESCAN_VALUES, which the wrapper sends for programChanged); without
    // it clap-validator reports "parameter values changed without a rescan request".
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
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
