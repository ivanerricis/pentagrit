// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Theme.h"

namespace pentagrit::ui
{
enum class Layout
{
    full,   // 1100 x 680, crossover display + strips
    compact // 1100 x 540, no crossover display, From/To fields per strip
};

enum class KnobMode
{
    vertical,
    circular
};

// Per-user interface options (not parameters, not part of presets). Fed by the settings (WP9) and applied
// live by PentagritAudioProcessorEditor::setUiOptions().
struct UiOptions
{
    Style style{Style::studioDark};
    Layout layout{Layout::full};
    bool tooltips = true;
    KnobMode knob{KnobMode::vertical};
    float valueStepDb = 0.0f;           // drag step for dB knobs: 0 (continuous), 0.1, 0.5 or 1
    float sensitivity = 0.5f;           // 0 (fine) .. 1 (fast), 0.5 = 170 px for the full range
    float meterHoldSec = 1.0f;          // peak-hold marker; 0 = off, < 0 = infinite
    float meterReleaseDbPerSec = 20.0f; // meter fall speed
    int refreshHz = 30;                 // timer rate (clamped to 10..120)
    bool showCpu = true;
    int zoomPercent = 100; // 75 / 100 / 125 / 150 / 200
};
} // namespace pentagrit::ui
