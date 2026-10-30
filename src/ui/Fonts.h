// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Theme.h"
#include <juce_graphics/juce_graphics.h>

namespace pentagrit::ui
{
// Per-style typography, embedded as BinaryData (all SIL OFL-1.1, see THIRD_PARTY_NOTICES.md):
//   Studio Dark    IBM Plex Sans / IBM Plex Mono
//   Analog Warm    Nunito Sans, Fraunces (display), DM Mono
//   Neon Lab       Chakra Petch, JetBrains Mono
//   Paper Light    Public Sans, Source Serif 4 (display), Source Code Pro
//   Industrial     Barlow, Barlow Condensed (display), Space Mono
//   Midnight Glass Manrope, Red Hat Mono
// Falls back to the system fonts when an embedded typeface cannot be loaded.
namespace fonts
{
enum class Weight
{
    regular,
    medium,
    semiBold
};

// Selects the font family set used by sans() / display() / mono() (process-wide, set by the look-and-feel).
void setStyle(Style s) noexcept;
Style currentStyle() noexcept;

// `px` is the CSS-like em size of the design; `tracking` is letter spacing in em.
juce::Font sans(float px, Weight w = Weight::regular, float tracking = 0.0f);
// Wordmark, band names and preset names (same as sans() in styles without a display face).
juce::Font display(float px, Weight w = Weight::regular, float tracking = 0.0f);
juce::Font mono(float px, Weight w = Weight::regular, float tracking = 0.0f);

// True when the embedded typefaces of the current style were created successfully.
bool embeddedFontsLoaded();
} // namespace fonts
} // namespace pentagrit::ui
