// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "presets/PresetManager.h"
#include <vector>

namespace pentagrit
{
struct FactoryPreset
{
    PresetEntry entry; // factory = true
    PresetParamValues values;
};

// Reads the factory presets embedded from resources/presets/*.xml, in menu order:
// categories Init, Drums, Bass, Mix Bus, Creative, then any other category; file name order inside a
// category.
std::vector<FactoryPreset> loadFactoryPresets();
} // namespace pentagrit
