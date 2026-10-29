// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pentagrit
{
// Message-thread preset manager. STUB (WP0): real implementation owned by WP8.
class PresetManager
{
public:
    explicit PresetManager(juce::AudioProcessorValueTreeState& apvts) : apvts_(apvts) {}

private:
    juce::AudioProcessorValueTreeState& apvts_;
};
} // namespace pentagrit
