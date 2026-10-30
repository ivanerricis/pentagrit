// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Fonts.h"

#include <PentagritUiData.h>
#include <atomic>
#include <memory>
#include <mutex>

namespace pentagrit::ui::fonts
{
namespace
{
struct Blob
{
    const char* data;
    int size;
};

#define PG_BLOB(name)                                                                                        \
    Blob                                                                                                     \
    {                                                                                                        \
        PentagritUiData::name##_ttf, PentagritUiData::name##_ttfSize                                         \
    }

// Font files per style: three sans weights (regular / medium / semiBold), three display weights, two mono
// weights (regular / bolder).
struct StyleFonts
{
    Blob sans[3], display[3], mono[2];
};

const StyleFonts& fontsFor(Style s)
{
    static const StyleFonts studio{
        {PG_BLOB(IBMPlexSansRegular), PG_BLOB(IBMPlexSansMedium), PG_BLOB(IBMPlexSansSemiBold)},
        {PG_BLOB(IBMPlexSansRegular), PG_BLOB(IBMPlexSansMedium), PG_BLOB(IBMPlexSansSemiBold)},
        {PG_BLOB(IBMPlexMonoRegular), PG_BLOB(IBMPlexMonoMedium)}};
    static const StyleFonts analog{
        {PG_BLOB(NunitoSansRegular), PG_BLOB(NunitoSansSemiBold), PG_BLOB(NunitoSansBold)},
        {PG_BLOB(FrauncesMedium), PG_BLOB(FrauncesMedium), PG_BLOB(FrauncesSemiBold)},
        {PG_BLOB(DMMonoRegular), PG_BLOB(DMMonoMedium)}};
    static const StyleFonts neon{
        {PG_BLOB(ChakraPetchRegular), PG_BLOB(ChakraPetchMedium), PG_BLOB(ChakraPetchSemiBold)},
        {PG_BLOB(ChakraPetchRegular), PG_BLOB(ChakraPetchMedium), PG_BLOB(ChakraPetchSemiBold)},
        {PG_BLOB(JetBrainsMonoRegular), PG_BLOB(JetBrainsMonoMedium)}};
    static const StyleFonts paper{
        {PG_BLOB(PublicSansRegular), PG_BLOB(PublicSansMedium), PG_BLOB(PublicSansSemiBold)},
        {PG_BLOB(SourceSerif4Medium), PG_BLOB(SourceSerif4Medium), PG_BLOB(SourceSerif4SemiBold)},
        {PG_BLOB(SourceCodeProRegular), PG_BLOB(SourceCodeProMedium)}};
    static const StyleFonts industrial{
        {PG_BLOB(BarlowRegular), PG_BLOB(BarlowMedium), PG_BLOB(BarlowSemiBold)},
        {PG_BLOB(BarlowCondensedSemiBold), PG_BLOB(BarlowCondensedSemiBold), PG_BLOB(BarlowCondensedBold)},
        {PG_BLOB(SpaceMonoRegular), PG_BLOB(SpaceMonoBold)}};
    static const StyleFonts midnight{
        {PG_BLOB(ManropeRegular), PG_BLOB(ManropeMedium), PG_BLOB(ManropeSemiBold)},
        {PG_BLOB(ManropeRegular), PG_BLOB(ManropeMedium), PG_BLOB(ManropeSemiBold)},
        {PG_BLOB(RedHatMonoRegular), PG_BLOB(RedHatMonoMedium)}};
    switch (s)
    {
        case Style::studioDark:
            return studio;
        case Style::analogWarm:
            return analog;
        case Style::neonLab:
            return neon;
        case Style::paperLight:
            return paper;
        case Style::industrial:
            return industrial;
        case Style::midnightGlass:
            return midnight;
    }
    return studio;
}

// Typefaces of one style, created on first use.
struct Faces
{
    juce::Typeface::Ptr sans[3], display[3], mono[2];
    bool ok = false;

    explicit Faces(Style s)
    {
        const auto& f = fontsFor(s);
        auto load = [](const Blob& b)
        { return juce::Typeface::createSystemTypefaceFor(b.data, static_cast<size_t>(b.size)); };
        ok = true;
        for (int i = 0; i < 3; ++i)
        {
            sans[i] = load(f.sans[i]);
            // reuse the same typeface when the display file is the sans file
            display[i] = f.display[i].data == f.sans[i].data ? sans[i] : load(f.display[i]);
            ok = ok && sans[i] != nullptr && display[i] != nullptr;
        }
        for (int i = 0; i < 2; ++i)
        {
            mono[i] = load(f.mono[i]);
            ok = ok && mono[i] != nullptr;
        }
    }
};

std::atomic<int> g_style{0};

const Faces& faces()
{
    static std::once_flag flags[kNumStyles];
    static std::unique_ptr<Faces> cache[kNumStyles];
    const int i = juce::jlimit(0, kNumStyles - 1, g_style.load());
    std::call_once(flags[i], [i] { cache[i] = std::make_unique<Faces>(static_cast<Style>(i)); });
    return *cache[i];
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

void setStyle(Style s) noexcept
{
    g_style.store(static_cast<int>(s));
}

Style currentStyle() noexcept
{
    return static_cast<Style>(g_style.load());
}

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

juce::Font display(float px, Weight w, float tracking)
{
    const auto& f = faces();
    return make(f.ok ? f.display[static_cast<int>(w)] : nullptr, "Segoe UI", w == Weight::semiBold, px,
                tracking);
}

juce::Font mono(float px, Weight w, float tracking)
{
    const auto& f = faces();
    return make(f.ok ? f.mono[w == Weight::regular ? 0 : 1] : nullptr, "Consolas", w == Weight::semiBold, px,
                tracking);
}
} // namespace pentagrit::ui::fonts
