// SPDX-License-Identifier: AGPL-3.0-or-later
// STUB (WP0): passthrough, latency 0. Real implementation owned by WP4.
#include "dsp/MultibandEngine.h"

namespace pentagrit
{
void MultibandEngine::prepare(double, int, int)
{
    latency_ = 0;
    meters_.latencySamples.store(0, std::memory_order_relaxed);
}

void MultibandEngine::reset() noexcept {}
void MultibandEngine::setSettings(const EngineSettings& settings) noexcept
{
    settings_ = settings;
}
void MultibandEngine::process(juce::AudioBuffer<float>&) noexcept {}
int MultibandEngine::getLatencySamples() const noexcept
{
    return latency_;
}

int MultibandEngine::effectiveFactor(OSFactor requested, double sampleRate) noexcept
{
    int factor = 1 << static_cast<int>(requested);
    while (factor > 1 && sampleRate * factor > 384000.0)
        factor /= 2;
    return factor;
}

std::array<bool, kMaxBands> MultibandEngine::computeAudibility(const EngineSettings& s) noexcept
{
    std::array<bool, kMaxBands> audible{};
    bool anySolo = false;
    for (int b = 0; b < s.numBands; ++b)
        anySolo = anySolo || s.bands[static_cast<size_t>(b)].solo;

    for (int b = 0; b < s.numBands; ++b)
    {
        const auto& band = s.bands[static_cast<size_t>(b)];
        audible[static_cast<size_t>(b)] = !band.mute && (!anySolo || band.solo);
    }
    return audible;
}
} // namespace pentagrit
