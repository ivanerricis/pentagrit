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

TEST_CASE("Editor layouts and styles", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);

    for (int bands : {3, 5})
        for (int si = 0; si < ui::kNumStyles; ++si)
            for (auto layout : {ui::Layout::full, ui::Layout::compact})
            {
                setValue(processor, pid::numBands, static_cast<float>(bands));
                const auto style = static_cast<ui::Style>(si);
                ui::UiOptions o;
                o.style = style;
                o.layout = layout;
                editor.setUiOptions(o);
                editor.refreshNow();

                const bool compact = layout == ui::Layout::compact;
                const juce::String name = juce::String(ui::styleId(style)) + "_" + juce::String(bands) +
                                          "b_" + (compact ? "compact" : "full");
                INFO(name);
                CHECK(editor.isCompact() == compact);
                CHECK(editor.getWidth() == ui::kBaseWidth);
                CHECK(editor.getHeight() == (compact ? ui::kCompactHeight : ui::kFullHeight));
                CHECK(editor.getCrossover().isVisible() == !compact);

                auto img = snapshot(editor, name);
                CHECK(img.getHeight() == editor.getHeight());
                CHECK(distinctColours(img) > 30);
                // The ground colour of the theme is present (top-left corner).
                CHECK(img.getPixelAt(1, 1) == ui::palette(style).bg0);
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
    o.valueStepDb = 0.5f;
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

namespace
{
juce::MouseEvent makeMouse(juce::Component& c, juce::ModifierKeys mods, int clicks = 1)
{
    const auto src = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Point<float> pos(10.0f, 10.0f);
    return {src,
            pos,
            mods,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &c,
            &c,
            juce::Time::getCurrentTime(),
            pos,
            juce::Time::getCurrentTime(),
            clicks,
            false};
}
} // namespace

TEST_CASE("Value step quantisation helper", "[gui][knob]")
{
    CHECK(ui::quantiseToStep(1.234, 0.0) == Catch::Approx(1.234));
    CHECK(ui::quantiseToStep(1.234, 0.1) == Catch::Approx(1.2));
    CHECK(ui::quantiseToStep(1.26, 0.5) == Catch::Approx(1.5));
    CHECK(ui::quantiseToStep(-1.26, 0.5) == Catch::Approx(-1.5));
    CHECK(ui::quantiseToStep(2.4, 1.0) == Catch::Approx(2.0));
    CHECK(ui::quantiseToStep(7.0, -1.0) == Catch::Approx(7.0));
}

TEST_CASE("Value step applies to dB knobs only", "[gui][knob]")
{
    PentagritAudioProcessor processor;
    ui::LabelledKnob db(processor.getAPVTS(), pid::inGain, "In", "hint", ui::LabelledKnob::Fmt::dbSignedUnit,
                        true, 44);
    ui::LabelledKnob pct(processor.getAPVTS(), pid::mix, "Mix", "hint", ui::LabelledKnob::Fmt::percent, false,
                         44);
    ui::UiOptions o;
    o.valueStepDb = 0.5f;
    db.applyOptions(o);
    pct.applyOptions(o);
    CHECK(db.slider().dragStep == Catch::Approx(0.5));
    CHECK(pct.slider().dragStep == 0.0);
    o.valueStepDb = 0.0f;
    db.applyOptions(o);
    CHECK(db.slider().dragStep == 0.0);
}

TEST_CASE("Ctrl+click resets a knob and typed values are not rounded", "[gui][knob]")
{
    PentagritAudioProcessor processor;
    ui::LabelledKnob knob(processor.getAPVTS(), pid::inGain, "In", "hint",
                          ui::LabelledKnob::Fmt::dbSignedUnit, true, 44);
    knob.setBounds(0, 0, 60, 80);
    ui::UiOptions o;
    o.valueStepDb = 1.0f;
    knob.applyOptions(o);

    knob.commitText("3.4");
    CHECK(knob.slider().getValue() == Catch::Approx(3.4).margin(0.011));
    CHECK(std::abs(raw(processor, pid::inGain) - 3.4f) < 0.011f);

    auto& s = knob.slider();
    s.mouseDown(makeMouse(s, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier |
                                                juce::ModifierKeys::ctrlModifier |
                                                juce::ModifierKeys::commandModifier)));
    s.mouseUp(makeMouse(s, juce::ModifierKeys()));
    CHECK(raw(processor, pid::inGain) == Catch::Approx(0.0f).margin(1.0e-4f));

    // Double-click requests the value editor; with Ctrl it does not.
    int requests = 0;
    s.onTypeRequest = [&requests] { ++requests; };
    s.mouseDoubleClick(makeMouse(s, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
    CHECK(requests == 1);
}

TEST_CASE("Crossover handle typed value and reset", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    MeterState meters;
    ui::CrossoverDisplay display(processor.getAPVTS(), meters);
    display.setBounds(0, 0, 800, 200);
    display.commitHz(0, 200.0f);
    CHECK(raw(processor, pid::xover[0]) == Catch::Approx(200.0f).margin(0.5f));
    display.resetHandle(0);
    CHECK(raw(processor, pid::xover[0]) == Catch::Approx(120.0f).margin(0.5f));
}

namespace
{
// Prepares the processor with the settings that give the longest latency (8x linear phase, 5 ms lookahead).
void prepareLongestLatency(PentagritAudioProcessor& processor, double sampleRate)
{
    setValue(processor, pid::oversampling, 3.0f);
    setValue(processor, pid::osPhase, 0.0f);
    setValue(processor, pid::lookahead, 3.0f);
    processor.setRateAndBufferSizeDetails(sampleRate, 512);
    processor.prepareToPlay(sampleRate, 512);
    processor.getMeters().latencySamples.store(processor.getLatencySamples());
}
} // namespace

TEST_CASE("Latency readout fits its area with a clear gap", "[gui][Editor]")
{
    for (double sampleRate : {44100.0, 192000.0})
    {
        for (int si = 0; si < ui::kNumStyles; ++si)
        {
            PentagritAudioProcessor processor;
            prepareLongestLatency(processor, sampleRate);
            REQUIRE(processor.getLatencySamples() > 100);
            PentagritAudioProcessorEditor editor(processor);

            for (int bands = 2; bands <= 5; ++bands)
            {
                for (auto layout : {ui::Layout::full, ui::Layout::compact})
                {
                    for (int zoom : {75, 100})
                    {
                        setValue(processor, pid::numBands, static_cast<float>(bands));
                        ui::UiOptions o;
                        o.style = static_cast<ui::Style>(si);
                        o.layout = layout;
                        o.zoomPercent = zoom;
                        editor.setUiOptions(o);
                        editor.refreshNow();

                        auto& top = editor.getTopBar();
                        INFO(sampleRate << " Hz, style " << si << ", " << bands << " bands, zoom " << zoom
                                        << ", text '" << top.latencyText() << "'");
                        CHECK(top.latencyText().contains("ms"));
                        CHECK(top.latencyText().contains(juce::String(processor.getLatencySamples())));

                        const auto area = top.latencyArea();
                        CHECK(area.getWidth() > 0);
                        CHECK(top.latencyTextWidth() <=
                              static_cast<float>(area.getWidth() - 2 * ui::TopBar::kLatencyPad));
                        // No neighbour overlaps the area (OS selector, AUTO-GAIN, preset slot, ...).
                        for (auto* child : top.getChildren())
                        {
                            if (child->isVisible())
                            {
                                CHECK_FALSE(child->getBounds().intersects(area));
                            }
                        }
                        CHECK(top.getLocalBounds().contains(area));
                        if (si == 0 && bands == 5 && zoom == 100)
                        {
                            snapshot(editor, "latency_longest_" + juce::String(static_cast<int>(sampleRate)) +
                                                 (layout == ui::Layout::full ? "_full" : "_compact"));
                        }
                        CHECK(top.presetSlot().getWidth() >= 300);
                    }
                }
            }
        }
    }
}

TEST_CASE("Transient knob captions keep a gap at 2 to 5 bands in every style", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    for (int bands : {2, 3, 4, 5})
    {
        for (int si = 0; si < ui::kNumStyles; ++si)
        {
            for (auto layout : {ui::Layout::full, ui::Layout::compact})
            {
                setValue(processor, pid::numBands, static_cast<float>(bands));
                ui::UiOptions o;
                o.style = static_cast<ui::Style>(si);
                o.layout = layout;
                editor.setUiOptions(o);
                editor.refreshNow();

                for (int b = 0; b < bands; ++b)
                {
                    const auto knobs = editor.getStrip(b).transientKnobs();
                    for (size_t i = 0; i < knobs.size(); ++i)
                    {
                        INFO("style " << si << ", " << bands << " bands, band " << b << ", "
                                      << knobs[i]->caption());
                        const float w = knobs[i]->captionWidth();
                        // The caption fits its own cell with a margin ...
                        CHECK(w <= static_cast<float>(knobs[i]->getWidth() - ui::LabelledKnob::kCaptionGap));
                        // ... so the clear space between two captions is at least the gap.
                        if (i > 0)
                        {
                            const auto* prev = knobs[i - 1];
                            const float prevRight = static_cast<float>(prev->getBounds().getCentreX()) +
                                                    prev->captionWidth() * 0.5f;
                            const float left =
                                static_cast<float>(knobs[i]->getBounds().getCentreX()) - w * 0.5f;
                            CHECK(left - prevRight >=
                                  static_cast<float>(ui::LabelledKnob::kCaptionGap) - 1.0f);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Gain-change meter holds the extremes and releases towards 0 dB", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    auto& m = processor.getMeters();
    const auto& ind = editor.getStrip(0).transientIndicator();

    ui::UiOptions o;
    o.meterHoldSec = -1.0f; // infinite hold
    o.meterReleaseDbPerSec = 1.0e6f;
    editor.setUiOptions(o);
    editor.refreshNow();
    CHECK(ind.displayMaxDb() == 0.0f);
    CHECK(ind.displayMinDb() == 0.0f);

    m.bandTransientGainMinDb[0].store(-3.0f);
    m.bandTransientGainMaxDb[0].store(6.0f);
    editor.refreshNow();
    CHECK(ind.displayMaxDb() == Catch::Approx(6.0f));
    CHECK(ind.displayMinDb() == Catch::Approx(-3.0f));
    CHECK(ind.readoutText() == "+6.0 dB");

    // Back to idle: the bar releases (fast here), the markers stay.
    m.bandTransientGainMinDb[0].store(0.0f);
    m.bandTransientGainMaxDb[0].store(0.0f);
    editor.refreshNow();
    CHECK(ind.displayMaxDb() == Catch::Approx(0.0f));
    CHECK(ind.displayMinDb() == Catch::Approx(0.0f));
    CHECK(ind.holdMaxDb() == Catch::Approx(6.0f));
    CHECK(ind.holdMinDb() == Catch::Approx(-3.0f));
    CHECK(ind.readoutText() == "+6.0 dB");

    // A deeper cut than the held boost becomes the readout.
    m.bandTransientGainMinDb[0].store(-9.0f);
    editor.refreshNow();
    CHECK(ind.holdMinDb() == Catch::Approx(-9.0f));
    CHECK(ind.readoutText() == "-9.0 dB");

    // Hold off: markers follow the bar.
    o.meterHoldSec = 0.0f;
    editor.setUiOptions(o);
    m.bandTransientGainMinDb[0].store(0.0f);
    editor.refreshNow();
    CHECK(ind.holdMinDb() == 0.0f);
    CHECK(ind.readoutText() == "0.0 dB");

    // Slow release: the bar falls gradually instead of jumping.
    o.meterHoldSec = 1.0f;
    o.meterReleaseDbPerSec = 1.0f;
    editor.setUiOptions(o);
    m.bandTransientGainMaxDb[0].store(12.0f);
    editor.refreshNow();
    m.bandTransientGainMaxDb[0].store(0.0f);
    editor.refreshNow();
    CHECK(ind.displayMaxDb() > 11.0f);
}

TEST_CASE("Gain-change meters stay readable at 5 bands in compact layout and every style", "[gui][Editor]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    setValue(processor, pid::numBands, 5.0f);
    for (int si = 0; si < ui::kNumStyles; ++si)
    {
        for (auto layout : {ui::Layout::full, ui::Layout::compact})
        {
            ui::UiOptions o;
            o.style = static_cast<ui::Style>(si);
            o.layout = layout;
            editor.setUiOptions(o);
            editor.refreshNow();
            for (int b = 0; b < 5; ++b)
            {
                const auto& ind = editor.getStrip(b).transientIndicator();
                INFO("style " << si << " band " << b);
                // Room for the readout (46 px) and a bar of at least 60 px.
                CHECK(ind.getWidth() >= 46 + 60);
                CHECK(ind.getHeight() >= 12);
            }
        }
    }
}

namespace
{
juce::Component* findByTitle(juce::Component& root, const juce::String& title)
{
    for (auto* child : root.getChildren())
    {
        if (child->getTitle() == title)
        {
            return child;
        }
        if (auto* found = findByTitle(*child, title))
        {
            return found;
        }
    }
    return nullptr;
}

// What a mouse click does (Button::triggerClick is asynchronous): flip the state, then run onClick.
void click(juce::ToggleButton& b)
{
    b.setToggleState(!b.getToggleState(), juce::dontSendNotification);
    b.onClick();
}
} // namespace

TEST_CASE("MONITOR row fits the master panel and its switches work", "[gui][Editor][delta][levelmatch]")
{
    PentagritAudioProcessor processor;
    const auto original = processor.getSettings().get();
    PentagritAudioProcessorEditor editor(processor);
    for (auto layout : {ui::Layout::full, ui::Layout::compact})
    {
        ui::UiOptions o;
        o.layout = layout;
        editor.setUiOptions(o);
        editor.refreshNow();

        auto* delta = dynamic_cast<juce::ToggleButton*>(findByTitle(editor, "Delta monitoring"));
        auto* match = dynamic_cast<juce::ToggleButton*>(findByTitle(editor, "Match bypass level"));
        auto* drive = findByTitle(editor, "Drive compensation");
        REQUIRE(delta != nullptr);
        REQUIRE(match != nullptr);
        REQUIRE(drive != nullptr);
        CHECK(delta->getTooltip().isNotEmpty());
        CHECK(match->getTooltip().isNotEmpty());

        auto* panel = delta->getParentComponent();
        REQUIRE(panel != nullptr);
        const auto inside = panel->getLocalBounds();
        INFO("layout " << (layout == ui::Layout::full ? "full" : "compact") << ", panel height "
                       << panel->getHeight());
        for (auto* c : {static_cast<juce::Component*>(delta), static_cast<juce::Component*>(match), drive})
        {
            CHECK(inside.contains(c->getBounds()));
            CHECK(c->getHeight() >= 20);
        }
        CHECK_FALSE(delta->getBounds().intersects(match->getBounds()));
        CHECK_FALSE(delta->getBounds().intersects(drive->getBounds()));
        CHECK_FALSE(match->getBounds().intersects(drive->getBounds()));
        // The switches drive the processor: delta is a session flag, the match option the shared settings.
        CHECK_FALSE(processor.isDeltaMonitoring());
        click(*delta);
        CHECK(processor.isDeltaMonitoring());
        editor.refreshNow();
        CHECK(delta->getToggleState());
        click(*delta);
        CHECK_FALSE(processor.isDeltaMonitoring());

        CHECK_FALSE(processor.isLevelMatchBypass());
        click(*match);
        CHECK(processor.isLevelMatchBypass());
        CHECK(processor.getSettings().get().levelMatchBypass);
        click(*match);
        CHECK_FALSE(processor.isLevelMatchBypass());
    }
    processor.getSettings().set(original);
}
