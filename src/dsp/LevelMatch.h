// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace pentagrit
{
// Level-matched bypass: learns how much louder (or quieter) the plugin output is than its input and applies
// that difference to the bypassed signal, so that bypass is a fair A/B comparison.
//
// Two one-pole mean-square trackers (about 2 s) follow the final output and the delayed input. They are only
// fed while the plugin is not bypassed (the caller decides) and while both blocks are above -60 dBFS RMS, so
// silence and bypass do not move the correction. The gain is the square root of the ratio, limited to
// +-24 dB, and reaches the audio through a 50 ms smoother. No allocation after prepare().
class LevelMatch
{
public:
    static constexpr float kMaxDb = 24.0f;
    static constexpr float kGateDb = -60.0f;

    void prepare(double sampleRate) noexcept;
    // Forgets what was learned (gain back to 0 dB, trackers empty).
    void reset() noexcept;

    // Feeds one block of the final output and of the delayed input (same channel count and length).
    // Channels are averaged. Blocks below the gate on either side are ignored.
    void analyse(const float* const* out, const float* const* ref, int numChannels, int numSamples) noexcept;

    // Moves the gain smoother on without touching any audio (blocks where the bypass path is not heard).
    void advance(int numSamples) noexcept;
    // Multiplies the block (the delayed input of the bypass path) by the smoothed gain.
    void apply(float* const* channels, int numChannels, int numSamples) noexcept;

    // Learned gain in dB (the target of the smoother), 0 until something was learned.
    float getTargetDb() const noexcept { return targetDb_; }

private:
    double rate_ = 44100.0;
    float tau_ = 0.0f; // per-sample pole of the trackers
    bool primed_ = false;
    float meanOut_ = 0.0f, meanRef_ = 0.0f;
    float targetDb_ = 0.0f;
    juce::SmoothedValue<float> gain_;
};
} // namespace pentagrit
