// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>

using namespace pentagrit;

namespace
{
void setValue(PentagritAudioProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getAPVTS().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

float raw(PentagritAudioProcessor& p, const juce::String& id)
{
    return p.getAPVTS().getRawParameterValue(id)->load();
}

// Number of distinct colours in a coarse sample grid of the image.
int distinctColours(const juce::Image& img)
{
    std::set<juce::uint32> colours;
    for (int y = 0; y < img.getHeight(); y += 3)
        for (int x = 0; x < img.getWidth(); x += 3)
            colours.insert(img.getPixelAt(x, y).getARGB());
    return static_cast<int>(colours.size());
}

juce::Image snapshot(PentagritAudioProcessorEditor& e, const juce::String& name)
{
    auto img = e.createComponentSnapshot(e.getLocalBounds(), false, 1.0f);
    const auto dir = juce::SystemStats::getEnvironmentVariable("PENTAGRIT_SNAPSHOT_DIR", {});
    if (dir.isNotEmpty())
    {
        juce::File out = juce::File(dir).getChildFile(name + ".png");
        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::FileOutputStream stream(out);
        if (stream.openedOk())
            juce::PNGImageFormat().writeImageToStream(img, stream);
    }
    return img;
}

int visibleStrips(PentagritAudioProcessorEditor& e)
{
    int n = 0;
    for (int b = 0; b < kMaxBands; ++b)
        n += e.getStrip(b).isVisible() ? 1 : 0;
    return n;
}
} // namespace

TEST_CASE("Editor create and destroy", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    {
        auto editor = std::make_unique<PentagritAudioProcessorEditor>(processor);
        CHECK(editor->getWidth() == ui::kBaseWidth);
        CHECK(editor->getHeight() == ui::kFullHeight);
        CHECK(editor->numBands() == 3);
        CHECK(visibleStrips(*editor) == 3);
        editor->refreshNow();
    }
    // A second editor can be created after the first is gone.
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    CHECK(editor->getWidth() == ui::kBaseWidth);
}

TEST_CASE("Editor zoom levels", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    for (int zoom : {75, 100, 125, 150, 200})
    {
        ui::UiOptions o;
        o.zoomPercent = zoom;
        editor.setUiOptions(o);
        INFO("zoom " << zoom);
        CHECK(editor.getTransform().mat00 == Catch::Approx(zoom / 100.0f).margin(0.001));
        // The logical (base) size does not change; the scale factor does the zooming.
        CHECK(editor.getWidth() == ui::kBaseWidth);
        editor.refreshNow();
        auto img = snapshot(editor, "zoom" + juce::String(zoom));
        CHECK(img.getWidth() == ui::kBaseWidth);
        CHECK(distinctColours(img) > 20);
    }
}

TEST_CASE("Editor layouts and themes", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);

    for (auto theme : {ui::Theme::dark, ui::Theme::light})
        for (auto layout : {ui::Layout::full, ui::Layout::compact})
        {
            ui::UiOptions o;
            o.theme = theme;
            o.layout = layout;
            editor.setUiOptions(o);
            editor.refreshNow();

            const bool compact = layout == ui::Layout::compact;
            const juce::String name = juce::String(theme == ui::Theme::dark ? "dark" : "light") + "_" +
                                      (compact ? "compact" : "full");
            INFO(name);
            CHECK(editor.isCompact() == compact);
            CHECK(editor.getWidth() == ui::kBaseWidth);
            CHECK(editor.getHeight() == (compact ? ui::kCompactHeight : ui::kFullHeight));
            CHECK(editor.getCrossover().isVisible() == !compact);

            auto img = snapshot(editor, name);
            CHECK(img.getHeight() == editor.getHeight());
            CHECK(distinctColours(img) > 30);
            // The ground colour of the theme is present (top-left corner).
            CHECK(img.getPixelAt(1, 1) == ui::palette(theme).bg0);
        }
}

TEST_CASE("Editor collapses to the number of bands", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    for (auto layout : {ui::Layout::full, ui::Layout::compact})
    {
        ui::UiOptions o;
        o.layout = layout;
        editor.setUiOptions(o);

        int lastWidth = 100000;
        for (int n = 2; n <= 5; ++n)
        {
            setValue(processor, pid::numBands, static_cast<float>(n));
            editor.refreshNow();
            INFO("bands " << n);
            CHECK(editor.numBands() == n);
            CHECK(visibleStrips(editor) == n);
            for (int b = 0; b < kMaxBands; ++b)
                CHECK(editor.getStrip(b).isVisible() == (b < n));
            // Active strips widen as the count falls (narrower when bands are added), never below 168 px.
            const int w = editor.getStrip(0).getWidth();
            CHECK(w < lastWidth);
            CHECK(w >= 168);
            lastWidth = w;
            auto img = snapshot(editor, juce::String(layout == ui::Layout::full ? "full" : "compact") +
                                            "_bands" + juce::String(n));
            CHECK(distinctColours(img) > 30);
        }
    }
}

TEST_CASE("Raising the band count keeps the crossover spacing", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    setValue(processor, pid::numBands, 2.0f);
    setValue(processor, pid::xover[0], 1000.0f);
    setValue(processor, pid::xover[1], 1050.0f);
    setValue(processor, pid::xover[2], 1060.0f);
    PentagritAudioProcessorEditor editor(processor);
    setValue(processor, pid::numBands, 5.0f);
    editor.refreshNow();
    for (size_t i = 1; i < 4; ++i)
        CHECK(raw(processor, pid::xover[i]) >= raw(processor, pid::xover[i - 1]) * ui::xover::ratio * 0.999f);
}

TEST_CASE("Editor options apply live", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    ui::UiOptions o;
    o.tooltips = false;
    o.dc = ui::DoubleClick::type;
    o.knob = ui::KnobMode::circular;
    o.sensitivity = 1.0f;
    o.meterHoldSec = 0.0f;
    o.meterReleaseDbPerSec = 40.0f;
    o.refreshHz = 60;
    o.showCpu = false;
    editor.setUiOptions(o);
    editor.refreshNow();
    CHECK(editor.getUiOptions().refreshHz == 60);
    CHECK_FALSE(editor.getUiOptions().showCpu);
    editor.setUiOptions(ui::UiOptions{});
    editor.refreshNow();
    CHECK(editor.getUiOptions().tooltips);
}

TEST_CASE("Meters and latency are read from MeterState", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    processor.getMeters().latencySamples.store(480);
    processor.getMeters().cpuLoad.store(0.032f);
    processor.getMeters().bandPeakDb[0].store(-6.0f);
    editor.refreshNow();
    CHECK(editor.getTopBar().latencyText().contains("480"));
    CHECK(editor.getFooter().cpuText() == "CPU 3.2%");
}

TEST_CASE("Editor resize keeps the aspect ratio and persists the size", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    {
        PentagritAudioProcessorEditor editor(processor);
        editor.setSize(825, 510);
        CHECK(processor.getUiSize().x == 825);
        CHECK(processor.getUiSize().y == 510);
    }
    PentagritAudioProcessorEditor again(processor);
    CHECK(again.getWidth() == 825);
}
