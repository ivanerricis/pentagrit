// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "params/ParamIDs.h"
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

namespace pentagrit::ui
{
// Links parameter edits made in the UI across bands.
//
// A band can be marked as linked (the mask is the int property `bandLinkMask` of the APVTS state: bit i is
// band i; it is saved with the project and survives presets and A/B because those keep unknown properties).
// While the user holds a gesture on a continuous parameter (gain, attack, sustain, speed, drive, mix) of a
// linked, active band, every other linked, active band gets the same offset in the normalised domain,
// clamped to 0..1, computed from the values at the start of the gesture (no drift when a sibling hits an
// end). satType is copied as is. mute, solo and bypass are never linked.
//
// The sibling gestures are opened while the leader's gesture is open, so the undo recorder of the processor
// folds all of them into one step. Only gestures drive the link: host automation, preset loads and the
// undo/redo itself (all without a leader gesture, or inside UndoManager::isPerformingUndoRedo) do not
// propagate. Message thread only; the mask is not part of the undo history.
class BandLink final : private juce::AudioProcessorParameter::Listener
{
public:
    static const juce::Identifier maskProperty; // "bandLinkMask"

    BandLink(juce::AudioProcessorValueTreeState& apvts, juce::UndoManager& undo);
    ~BandLink() override;

    BandLink(const BandLink&) = delete;
    BandLink& operator=(const BandLink&) = delete;

    int mask() const;
    bool isLinked(int band) const;
    void setLinked(int band, bool linked);

    // True while a leader gesture is open (tests).
    bool isGestureOpen() const noexcept { return leader_ >= 0; }

private:
    struct Slot
    {
        int band = -1;
        int kind = -1; // index in kLinkedParams
    };

    void parameterValueChanged(int index, float newValue) override;
    void parameterGestureChanged(int index, bool starting) override;

    bool numBandsActive(int band) const;

    juce::AudioProcessorValueTreeState& apvts_;
    juce::UndoManager& undo_;
    // Parameter index -> (band, kind); band -1 for the parameters that are not linked.
    std::vector<Slot> slots_;
    std::vector<juce::AudioProcessorParameter*> listened_;

    bool guard_ = false;
    int leader_ = -1; // parameter index of the open leader gesture
    int leaderKind_ = -1;
    float leaderStart_ = 0.0f;
    struct Sibling
    {
        juce::AudioProcessorParameter* param = nullptr;
        float start = 0.0f;
    };
    std::array<Sibling, kMaxBands> siblings_{};
    int numSiblings_ = 0;
};
} // namespace pentagrit::ui
