// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace pentagrit::ui
{
// IBM Plex Sans / Mono embedded as BinaryData (SIL OFL-1.1). Falls back to the system fonts when the embedded
// typefaces cannot be loaded.
namespace fonts
{
enum class Weight
{
    regular,
    medium,
    semiBold
};

// `px` is the CSS-like em size of the design; `tracking` is letter spacing in em.
juce::Font sans(float px, Weight w = Weight::regular, float tracking = 0.0f);
juce::Font mono(float px, Weight w = Weight::regular, float tracking = 0.0f);

// True when the embedded Plex typefaces were created successfully.
bool embeddedFontsLoaded();
} // namespace fonts
} // namespace pentagrit::ui
