// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace pentagrit
{
// Placeholder editor wrapping JUCE's generic editor. Real UI owned by WP5b.
class PentagritAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PentagritAudioProcessorEditor(PentagritAudioProcessor& processor);

    void resized() override;

private:
    PentagritAudioProcessor& processor_;
    juce::GenericAudioProcessorEditor generic_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PentagritAudioProcessorEditor)
};
} // namespace pentagrit
