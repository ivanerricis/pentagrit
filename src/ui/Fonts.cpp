// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Fonts.h"

#include <PentagritUiData.h>

namespace pentagrit::ui::fonts
{
namespace
{
struct Faces
{
    juce::Typeface::Ptr sans[3], mono[2];
    bool ok = false;

    Faces()
    {
        using namespace PentagritUiData;
        sans[0] = juce::Typeface::createSystemTypefaceFor(IBMPlexSansRegular_ttf, IBMPlexSansRegular_ttfSize);
        sans[1] = juce::Typeface::createSystemTypefaceFor(IBMPlexSansMedium_ttf, IBMPlexSansMedium_ttfSize);
        sans[2] =
            juce::Typeface::createSystemTypefaceFor(IBMPlexSansSemiBold_ttf, IBMPlexSansSemiBold_ttfSize);
        mono[0] = juce::Typeface::createSystemTypefaceFor(IBMPlexMonoRegular_ttf, IBMPlexMonoRegular_ttfSize);
        mono[1] = juce::Typeface::createSystemTypefaceFor(IBMPlexMonoMedium_ttf, IBMPlexMonoMedium_ttfSize);
        ok = sans[0] != nullptr && sans[1] != nullptr && sans[2] != nullptr && mono[0] != nullptr &&
             mono[1] != nullptr;
    }
};

const Faces& faces()
{
    static const Faces f;
    return f;
}

juce::Font make(const juce::Typeface::Ptr& face, const juce::String& fallbackFamily, bool bold, float px,
                float tracking)
{
    juce::FontOptions opts = face != nullptr
                                 ? juce::FontOptions(face)
                                 : juce::FontOptions(fallbackFamily, bold ? "Bold" : "Regular", 1.0f);
    opts = opts.withPointHeight(px);
    juce::Font f(opts);
    if (tracking != 0.0f)
        f.setExtraKerningFactor(tracking);
    return f;
}
} // namespace

bool embeddedFontsLoaded()
{
    return faces().ok;
}

juce::Font sans(float px, Weight w, float tracking)
{
    const auto& f = faces();
    return make(f.ok ? f.sans[static_cast<int>(w)] : nullptr, "Segoe UI", w == Weight::semiBold, px,
                tracking);
}

juce::Font mono(float px, Weight w, float tracking)
{
    const auto& f = faces();
    return make(f.ok ? f.mono[w == Weight::regular ? 0 : 1] : nullptr, "Consolas", w == Weight::semiBold, px,
                tracking);
}
} // namespace pentagrit::ui::fonts
