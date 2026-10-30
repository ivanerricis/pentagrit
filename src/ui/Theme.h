// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "params/ParamIDs.h"
#include <array>
#include <juce_graphics/juce_graphics.h>

namespace pentagrit::ui
{
enum class Style
{
    studioDark,
    analogWarm,
    neonLab,
    paperLight,
    industrial,
    midnightGlass
};

inline constexpr int kNumStyles = 6;

enum class KnobShape
{
    line,     // dark hub with a pointer line (Studio Dark)
    bigCap,   // large ink cap seen from above (Analog Warm)
    ring,     // thin outline ring with a short tick (Neon Lab)
    whiteCap, // outlined light cap with an accent pointer (Paper Light)
    heavy,    // thick butt-capped arc, grey hub, square pointer (Industrial)
    thumbDot  // fat round arc with a thumb dot at its end, no pointer (Midnight Glass)
};

// Colour and simple shape tokens from the approved designs (C:\dev\pt-design\styles\STYLES.md).
struct Palette
{
    juce::Colour bg0, bg1, bg2, bg3, bg4;
    juce::Colour ink, ink2, ink3;
    std::array<juce::Colour, kMaxBands> band;
    juce::Colour mute, solo, bypass, warn, modified;
    juce::Colour act;    // active toggle / selected fill
    juce::Colour onAct;  // text on `act`
    juce::Colour onBand; // text on mute / solo / bypass fills
    // Shape tokens.
    float rSmall, rButton, rWell, rPanel; // corner radii (small control, button, well, panel)
    float controlBorderW;                 // outline width of controls (0 = none), drawn in bg4
    float panelBorderW;                   // outline width of panels (0 = none), drawn in bg3
    KnobShape knob;
    float knobTrackW, knobArcW; // knob track and value-arc stroke widths
};

inline const char* styleName(Style s) noexcept
{
    switch (s)
    {
        case Style::studioDark:
            return "Studio Dark";
        case Style::analogWarm:
            return "Analog Warm";
        case Style::neonLab:
            return "Neon Lab";
        case Style::paperLight:
            return "Paper Light";
        case Style::industrial:
            return "Industrial";
        case Style::midnightGlass:
            return "Midnight Glass";
    }
    return "Studio Dark";
}

// Stable identifier stored in the settings file.
inline const char* styleId(Style s) noexcept
{
    switch (s)
    {
        case Style::studioDark:
            return "studioDark";
        case Style::analogWarm:
            return "analogWarm";
        case Style::neonLab:
            return "neonLab";
        case Style::paperLight:
            return "paperLight";
        case Style::industrial:
            return "industrial";
        case Style::midnightGlass:
            return "midnightGlass";
    }
    return "studioDark";
}

// Parses a stored value. Legacy "dark" -> Studio Dark, "light" -> Paper Light, anything else -> Studio Dark.
inline Style styleFromId(const juce::String& id) noexcept
{
    for (int i = 0; i < kNumStyles; ++i)
        if (id == styleId(static_cast<Style>(i)))
            return static_cast<Style>(i);
    if (id == "light")
        return Style::paperLight;
    return Style::studioDark;
}

inline const Palette& palette(Style st) noexcept
{
    using juce::Colour;
    static const Palette studioDark{
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
        Colour(0xFFFFB347),
        Colour(0xFFE8ECF2),
        Colour(0xFF0E1013),
        Colour(0xFF0E1013),
        3.0f,
        4.0f,
        4.0f,
        6.0f,
        0.0f,
        0.0f,
        KnobShape::line,
        4.0f,
        4.0f};
    static const Palette analogWarm{
        Colour(0xFF1B1815),
        Colour(0xFF26221E),
        Colour(0xFF332E28),
        Colour(0xFF443D34),
        Colour(0xFF5B5142),
        Colour(0xFFF2E8D2),
        Colour(0xFFC0B398),
        Colour(0xFF8C8068),
        {Colour(0xFFE0674F), Colour(0xFFE5A042), Colour(0xFFD9CB72), Colour(0xFF86B98F), Colour(0xFF6FA3BC)},
        Colour(0xFFE0674F),
        Colour(0xFFD9CB72),
        Colour(0xFF6FA3BC),
        Colour(0xFFE0674F),
        Colour(0xFFD9A441),
        Colour(0xFFD9A441),
        Colour(0xFF1B1815),
        Colour(0xFF1B1815),
        5.0f,
        8.0f,
        8.0f,
        10.0f,
        0.0f,
        0.0f,
        KnobShape::bigCap,
        2.5f,
        3.0f};
    static const Palette neonLab{
        Colour(0xFF05060A),
        Colour(0xFF0A0C12),
        Colour(0xFF10131C),
        Colour(0xFF1B2030),
        Colour(0xFF2C3550),
        Colour(0xFFE6F6FF),
        Colour(0xFF8FA6BC),
        Colour(0xFF5E7088),
        {Colour(0xFFFF3D7F), Colour(0xFFFFB020), Colour(0xFFC6FF1A), Colour(0xFF19F0FF), Colour(0xFFA06BFF)},
        Colour(0xFFFF3D7F),
        Colour(0xFFC6FF1A),
        Colour(0xFF19F0FF),
        Colour(0xFFFF3D7F),
        Colour(0xFFFFB020),
        Colour(0xFF19F0FF),
        Colour(0xFF05060A),
        Colour(0xFF05060A),
        1.0f,
        1.0f,
        2.0f,
        2.0f,
        1.0f,
        1.0f,
        KnobShape::ring,
        1.5f,
        2.6f};
    static const Palette paperLight{
        Colour(0xFFE7E9EE),
        Colour(0xFFFBFBF9),
        Colour(0xFFEDEFF3),
        Colour(0xFFD9DEE7),
        Colour(0xFFBCC4D2),
        Colour(0xFF121A33),
        Colour(0xFF454F6B),
        Colour(0xFF6A7490),
        {Colour(0xFFD6383F), Colour(0xFFD9780F), Colour(0xFF8F7600), Colour(0xFF138A67), Colour(0xFF2158C9)},
        Colour(0xFFC42B32),
        Colour(0xFF8A6400),
        Colour(0xFF2158C9),
        Colour(0xFFC42B32),
        Colour(0xFFD9780F),
        Colour(0xFF1B3A8A),
        Colour(0xFFFFFFFF),
        Colour(0xFFFFFFFF),
        2.0f,
        3.0f,
        3.0f,
        4.0f,
        1.0f,
        1.0f,
        KnobShape::whiteCap,
        3.0f,
        3.0f};
    static const Palette industrial{
        Colour(0xFF090909),
        Colour(0xFF141414),
        Colour(0xFF1F1F1F),
        Colour(0xFF303030),
        Colour(0xFF4A4A4A),
        Colour(0xFFF3F3F0),
        Colour(0xFFB0B0AA),
        Colour(0xFF7C7C76),
        {Colour(0xFFFF3B30), Colour(0xFFFF7A00), Colour(0xFFFFD100), Colour(0xFFC9C9C0), Colour(0xFF4FB3FF)},
        Colour(0xFFFF3B30),
        Colour(0xFFFFD100),
        Colour(0xFF4FB3FF),
        Colour(0xFFFF3B30),
        Colour(0xFFFF7A00),
        Colour(0xFFFF5F00),
        Colour(0xFF090909),
        Colour(0xFF090909),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        2.0f,
        2.0f,
        KnobShape::heavy,
        6.0f,
        6.0f};
    static const Palette midnightGlass{
        Colour(0xFF0C0D21),
        Colour(0xFF14163A),
        Colour(0xFF1D2050),
        Colour(0xFF2B2F6B),
        Colour(0xFF40469A),
        Colour(0xFFEEF0FF),
        Colour(0xFFAAB0E0),
        Colour(0xFF7C82B8),
        {Colour(0xFFFF9AA8), Colour(0xFFFFC890), Colour(0xFFF4EE9A), Colour(0xFF8FE3C5), Colour(0xFF8DB8FF)},
        Colour(0xFFFF9AA8),
        Colour(0xFFF4EE9A),
        Colour(0xFF8DB8FF),
        Colour(0xFFFF9AA8),
        Colour(0xFFFFC890),
        Colour(0xFFC9C4FF),
        Colour(0xFF0C0D21),
        Colour(0xFF0C0D21),
        12.0f,
        16.0f,
        12.0f,
        16.0f,
        0.0f,
        0.0f,
        KnobShape::thumbDot,
        5.0f,
        5.0f};
    switch (st)
    {
        case Style::studioDark:
            return studioDark;
        case Style::analogWarm:
            return analogWarm;
        case Style::neonLab:
            return neonLab;
        case Style::paperLight:
            return paperLight;
        case Style::industrial:
            return industrial;
        case Style::midnightGlass:
            return midnightGlass;
    }
    return studioDark;
}

// Fills a panel (bg1) with the style's corner radius and optional outline.
inline void fillPanel(juce::Graphics& g, juce::Rectangle<float> r, const Palette& p)
{
    g.setColour(p.bg1);
    g.fillRoundedRectangle(r, p.rPanel);
    if (p.panelBorderW > 0.0f)
    {
        g.setColour(p.bg3);
        g.drawRoundedRectangle(r.reduced(p.panelBorderW * 0.5f), p.rPanel, p.panelBorderW);
    }
}

// Base (logical) editor sizes.
inline constexpr int kBaseWidth = 1100;
inline constexpr int kFullHeight = 680;
inline constexpr int kCompactHeight = 540;
} // namespace pentagrit::ui
