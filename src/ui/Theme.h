// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "params/ParamIDs.h"
#include <array>
#include <juce_graphics/juce_graphics.h>

namespace pentagrit::ui
{
enum class Theme
{
    dark,
    light
};

// Colour tokens from the approved design (C:\dev\pt-design\THEME.md).
struct Palette
{
    juce::Colour bg0, bg1, bg2, bg3, bg4;
    juce::Colour ink, ink2, ink3;
    std::array<juce::Colour, kMaxBands> band;
    juce::Colour mute, solo, bypass, warn, modified;
};

inline const Palette& palette(Theme t) noexcept
{
    using juce::Colour;
    static const Palette dark{
        Colour(0xFF0E1013),
        Colour(0xFF151920),
        Colour(0xFF1D222B),
        Colour(0xFF2A313D),
        Colour(0xFF394252),
        Colour(0xFFE8ECF2),
        Colour(0xFF98A2B3),
        Colour(0xFF5F6A7C),
        {Colour(0xFFFF5A5F), Colour(0xFFFFB347), Colour(0xFFF9F871), Colour(0xFF4CD7A6), Colour(0xFF4DA3FF)},
        Colour(0xFFFF5A5F),
        Colour(0xFFF9F871),
        Colour(0xFF4DA3FF),
        Colour(0xFFFF5A5F),
        Colour(0xFFFFB347)};
    static const Palette light{
        Colour(0xFFE6EAF0),
        Colour(0xFFF6F8FA),
        Colour(0xFFE4E9EF),
        Colour(0xFFCDD5DF),
        Colour(0xFFB6C1CE),
        Colour(0xFF14181F),
        Colour(0xFF4A5566),
        Colour(0xFF76808F),
        {Colour(0xFFE5484D), Colour(0xFFE58A00), Colour(0xFFB8A400), Colour(0xFF14A478), Colour(0xFF1F7FE0)},
        Colour(0xFFE5484D),
        Colour(0xFFB8A400),
        Colour(0xFF1F7FE0),
        Colour(0xFFE5484D),
        Colour(0xFFE58A00)};
    return t == Theme::dark ? dark : light;
}

// Base (logical) editor sizes.
inline constexpr int kBaseWidth = 1100;
inline constexpr int kFullHeight = 680;
inline constexpr int kCompactHeight = 540;
} // namespace pentagrit::ui
