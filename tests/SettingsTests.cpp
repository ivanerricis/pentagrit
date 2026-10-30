// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"
#include "settings/SettingsManager.h"
#include "ui/SettingsPanel.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <set>

using namespace pentagrit;

namespace
{
// All processors created by the test suite use a throw-away, never written settings file, and do not record
// the last used preset (so presets loaded by one test never leak into the next processor).
[[maybe_unused]] const bool kOverridesInstalled = []
{
    SettingsManager::setTestOverrides(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                          .getChildFile("pentagrit_tests_unused_settings.settings"),
                                      true, true);
    return true;
}();

struct TempFile
{
    juce::File file = juce::File::createTempFile(".settings");
    ~TempFile() { file.deleteFile(); }
};

// RAII: records the last used preset during one test.
struct TrackLastPreset
{
    TrackLastPreset() { SettingsManager::setIgnoreLastPreset(false); }
    ~TrackLastPreset() { SettingsManager::setIgnoreLastPreset(true); }
};

int distinctColours(const juce::Image& img)
{
    std::set<juce::uint32> colours;
    for (int y = 0; y < img.getHeight(); y += 3)
        for (int x = 0; x < img.getWidth(); x += 3)
            colours.insert(img.getPixelAt(x, y).getARGB());
    return static_cast<int>(colours.size());
}

juce::Image snapshot(juce::Component& c, const juce::String& name)
{
    auto img = c.createComponentSnapshot(c.getLocalBounds(), false, 1.0f);
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

PresetEntry secondFactory(PresetManager& pm)
{
    const auto& f = pm.getFactoryPresets();
    REQUIRE(f.size() > 2);
    return f[2];
}
} // namespace

TEST_CASE("Settings defaults", "[settings]")
{
    TempFile tmp;
    SettingsManager m(tmp.file);
    const auto& s = m.get();
    CHECK(s.zoomPercent == 100);
    CHECK(s.style == ui::Style::studioDark);
    CHECK(s.layout == ui::Layout::full);
    CHECK(s.tooltips);
    CHECK(s.doubleClick == ui::DoubleClick::reset);
    CHECK(s.knobMode == ui::KnobMode::vertical);
    CHECK(s.sensitivity == Catch::Approx(0.5f));
    CHECK(s.meterHoldMs == 1000);
    CHECK(s.meterRelease == Catch::Approx(20.0f));
    CHECK(s.meterRefreshHz == 30);
    CHECK(s.showCpu);
    CHECK(s.presetFolder.isEmpty());
    CHECK(s.startupMode == StartupMode::last);
    CHECK_FALSE(s.checkUpdates); // opt-in
    CHECK(m.getEffectivePresetFolder() == PresetManager::getDefaultUserFolder());
    CHECK(m.loadedOk());
    CHECK_FALSE(SettingsManager::kUpdateCheckAvailable);
}

TEST_CASE("Settings default file location", "[settings]")
{
    SettingsManager::setTestOverrides({}, false, false);
    const auto f = SettingsManager::getDefaultFile();
    SettingsManager::setTestOverrides(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                          .getChildFile("pentagrit_tests_unused_settings.settings"),
                                      true, true);
    CHECK(f.getFileName() == "settings.settings");
    CHECK(f.getParentDirectory().getFileName() == "Pentagrit");
}

TEST_CASE("Settings round trip", "[settings]")
{
    TempFile tmp;
    {
        SettingsManager m(tmp.file);
        auto s = m.get();
        s.zoomPercent = 150;
        s.style = ui::Style::paperLight;
        s.layout = ui::Layout::compact;
        s.tooltips = false;
        s.doubleClick = ui::DoubleClick::type;
        s.knobMode = ui::KnobMode::circular;
        s.sensitivity = 0.8f;
        s.meterHoldMs = UserSettings::kInfiniteHold;
        s.meterRelease = 35.0f;
        s.meterRefreshHz = 60;
        s.showCpu = false;
        s.presetFolder = "C:/some/where";
        s.startupMode = StartupMode::specific;
        s.startup = {"Punchy Kick", "Drums", true};
        s.lastPreset = {"My Preset", "Unsorted", false};
        s.checkUpdates = true;
        m.set(s);
    }
    REQUIRE(tmp.file.existsAsFile());
    SettingsManager m(tmp.file);
    const auto& s = m.get();
    CHECK(s.zoomPercent == 150);
    CHECK(s.style == ui::Style::paperLight);
    CHECK(s.layout == ui::Layout::compact);
    CHECK_FALSE(s.tooltips);
    CHECK(s.doubleClick == ui::DoubleClick::type);
    CHECK(s.knobMode == ui::KnobMode::circular);
    CHECK(s.sensitivity == Catch::Approx(0.8f));
    CHECK(s.meterHoldMs == UserSettings::kInfiniteHold);
    CHECK(s.meterRelease == Catch::Approx(35.0f));
    CHECK(s.meterRefreshHz == 60);
    CHECK_FALSE(s.showCpu);
    CHECK(s.presetFolder == "C:/some/where");
    CHECK(s.startupMode == StartupMode::specific);
    CHECK(s.startup == PresetRef{"Punchy Kick", "Drums", true});
    CHECK(s.lastPreset == PresetRef{"My Preset", "Unsorted", false});
    CHECK(s.checkUpdates);
    CHECK(m.getEffectivePresetFolder() == juce::File("C:/some/where"));
}

TEST_CASE("Settings corrupt or missing file is tolerated", "[settings]")
{
    TempFile tmp;
    SECTION("missing")
    {
        SettingsManager m(tmp.file);
        CHECK(m.get().zoomPercent == 100);
        CHECK(m.loadedOk());
    }
    SECTION("garbage")
    {
        tmp.file.replaceWithText("this is <<< not xml \x01\x02 &&&");
        SettingsManager m(tmp.file);
        CHECK_FALSE(m.loadedOk());
        CHECK(m.get().zoomPercent == 100);
        CHECK(m.get().style == ui::Style::studioDark);
        auto s = m.get();
        s.zoomPercent = 125; // can be saved over the corrupt file
        m.set(s);
        SettingsManager again(tmp.file);
        CHECK(again.loadedOk());
        CHECK(again.get().zoomPercent == 125);
    }
    SECTION("wrong values are sanitised")
    {
        tmp.file.replaceWithText(
            "<PROPERTIES><VALUE name=\"uiZoom\" val=\"137\"/><VALUE name=\"knobSens\" "
            "val=\"9\"/><VALUE name=\"meterRefresh\" val=\"45\"/><VALUE name=\"meterRelease\" "
            "val=\"-3\"/><VALUE name=\"uiTheme\" val=\"purple\"/><VALUE "
            "name=\"startupMode\" val=\"specific\"/></PROPERTIES>");
        SettingsManager m(tmp.file);
        CHECK(m.get().zoomPercent == 100);
        CHECK(m.get().sensitivity == Catch::Approx(1.0f));
        CHECK(m.get().meterRefreshHz == 30);
        CHECK(m.get().meterRelease == Catch::Approx(5.0f));
        CHECK(m.get().style == ui::Style::studioDark);
        CHECK(m.get().startupMode == StartupMode::last); // "specific" without a preset
    }
    SECTION("set() validates")
    {
        SettingsManager m(tmp.file);
        auto s = m.get();
        s.zoomPercent = 999;
        s.meterRefreshHz = 17;
        m.set(s);
        CHECK(m.get().zoomPercent == 100);
        CHECK(m.get().meterRefreshHz == 30);
    }
}

TEST_CASE("Settings reset keeps the last preset", "[settings]")
{
    TempFile tmp;
    SettingsManager m(tmp.file);
    auto s = m.get();
    s.zoomPercent = 200;
    s.checkUpdates = true;
    s.presetFolder = "X:/y";
    s.lastPreset = {"Kept", "Cat", false};
    m.set(s);

    int notified = 0;
    struct L : juce::ChangeListener
    {
        int* n;
        void changeListenerCallback(juce::ChangeBroadcaster*) override { ++*n; }
    } listener;
    listener.n = &notified;
    m.addChangeListener(&listener);

    m.resetToDefaults();
    CHECK(notified == 1);
    CHECK(m.get().zoomPercent == 100);
    CHECK_FALSE(m.get().checkUpdates);
    CHECK(m.get().presetFolder.isEmpty());
    CHECK(m.get().lastPreset == PresetRef{"Kept", "Cat", false});
    SettingsManager again(tmp.file);
    CHECK(again.get().zoomPercent == 100);
    CHECK(again.get().lastPreset.name == "Kept");
    m.removeChangeListener(&listener);
}

TEST_CASE("Settings reload when the file changes", "[settings]")
{
    TempFile tmp;
    SettingsManager a(tmp.file);
    SettingsManager b(tmp.file);
    CHECK_FALSE(a.reloadIfChanged());
    auto s = b.get();
    s.zoomPercent = 75;
    juce::Thread::sleep(20);
    b.set(s);
    // The file time resolution can hide a very fast change; force it to differ.
    tmp.file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
    CHECK(a.reloadIfChanged());
    CHECK(a.get().zoomPercent == 75);
}

TEST_CASE("Settings convert to UiOptions", "[settings]")
{
    UserSettings s;
    s.zoomPercent = 125;
    s.style = ui::Style::paperLight;
    s.layout = ui::Layout::compact;
    s.tooltips = false;
    s.doubleClick = ui::DoubleClick::type;
    s.knobMode = ui::KnobMode::circular;
    s.sensitivity = 0.25f;
    s.meterHoldMs = 500;
    s.meterRelease = 40.0f;
    s.meterRefreshHz = 60;
    s.showCpu = false;
    auto o = SettingsManager::toUiOptions(s);
    CHECK(o.zoomPercent == 125);
    CHECK(o.style == ui::Style::paperLight);
    CHECK(o.layout == ui::Layout::compact);
    CHECK_FALSE(o.tooltips);
    CHECK(o.dc == ui::DoubleClick::type);
    CHECK(o.knob == ui::KnobMode::circular);
    CHECK(o.sensitivity == Catch::Approx(0.25f));
    CHECK(o.meterHoldSec == Catch::Approx(0.5f));
    CHECK(o.meterReleaseDbPerSec == Catch::Approx(40.0f));
    CHECK(o.refreshHz == 60);
    CHECK_FALSE(o.showCpu);

    s.meterHoldMs = 0;
    CHECK(SettingsManager::toUiOptions(s).meterHoldSec == 0.0f);
    s.meterHoldMs = UserSettings::kInfiniteHold;
    CHECK(SettingsManager::toUiOptions(s).meterHoldSec < 0.0f);

    // Defaults match the editor defaults.
    const auto d = SettingsManager::toUiOptions(UserSettings{});
    const ui::UiOptions e;
    CHECK(d.zoomPercent == e.zoomPercent);
    CHECK(d.refreshHz == e.refreshHz);
    CHECK(d.meterHoldSec == Catch::Approx(e.meterHoldSec));
    CHECK(d.sensitivity == Catch::Approx(e.sensitivity));
}

TEST_CASE("Startup preset policy", "[settings][presets]")
{
    PentagritAudioProcessor processor;
    auto& pm = processor.getPresetManager();
    const auto target = secondFactory(pm);
    REQUIRE(target.name != "Init");

    UserSettings s;
    SECTION("last without history does nothing")
    {
        CHECK_FALSE(SettingsManager::applyStartupPreset(s, pm));
        CHECK(pm.getCurrentName() == "Init");
    }
    SECTION("last")
    {
        s.lastPreset = SettingsManager::toRef(target);
        CHECK(SettingsManager::applyStartupPreset(s, pm));
        CHECK(pm.getCurrentName() == target.name);
    }
    SECTION("specific")
    {
        s.startupMode = StartupMode::specific;
        s.startup = SettingsManager::toRef(target);
        s.lastPreset = {"Init", "Init", true};
        CHECK(SettingsManager::applyStartupPreset(s, pm));
        CHECK(pm.getCurrentName() == target.name);
    }
    SECTION("init")
    {
        REQUIRE(pm.loadPreset(target).wasOk());
        s.startupMode = StartupMode::init;
        s.lastPreset = SettingsManager::toRef(target);
        CHECK(SettingsManager::applyStartupPreset(s, pm));
        CHECK(pm.getCurrentName() == "Init");
    }
    SECTION("missing preset is ignored")
    {
        s.startupMode = StartupMode::specific;
        s.startup = {"Does not exist", "Nowhere", false};
        CHECK_FALSE(SettingsManager::applyStartupPreset(s, pm));
        CHECK(pm.getCurrentName() == "Init");
    }
}

TEST_CASE("Processor applies settings: startup preset, restored state wins, user folder, last preset",
          "[settings][presets]")
{
    TrackLastPreset tracking;
    PentagritAudioProcessor first;
    auto& settings = first.getSettings();
    const auto target = secondFactory(first.getPresetManager());

    SECTION("last used preset is recorded")
    {
        REQUIRE(first.getPresetManager().loadPreset(target).wasOk());
        CHECK(settings.get().lastPreset == SettingsManager::toRef(target));
    }

    SECTION("startup preset on creation, but a restored state wins")
    {
        juce::MemoryBlock initState;
        first.getStateInformation(initState); // Init

        auto s = settings.get();
        s.startupMode = StartupMode::specific;
        s.startup = SettingsManager::toRef(target);
        settings.set(s);

        PentagritAudioProcessor second; // shares the settings with `first`
        CHECK(second.getPresetManager().getCurrentName() == target.name);
        second.setStateInformation(initState.getData(), static_cast<int>(initState.getSize()));
        CHECK(second.getPresetManager().getCurrentName() == "Init");
        settings.resetToDefaults();
    }

    SECTION("user folder follows the settings")
    {
        const auto dir =
            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("pg_settings_presets");
        auto s = settings.get();
        s.presetFolder = dir.getFullPathName();
        settings.set(s);
        CHECK(first.getPresetManager().getUserFolder() == dir);
        settings.resetToDefaults();
        CHECK(first.getPresetManager().getUserFolder() == PresetManager::getDefaultUserFolder());
    }
    settings.setLastPreset({});
}

TEST_CASE("Settings panel create, switch tabs, destroy", "[gui][settings]")
{
    PentagritAudioProcessor processor;
    {
        auto panel = std::make_unique<ui::SettingsPanel>(processor);
        panel->setSize(ui::kBaseWidth, ui::kFullHeight);
        CHECK(panel->currentTab() == ui::SettingsPanel::tabInterface);
        for (int t = 0; t < ui::SettingsPanel::numTabs; ++t)
        {
            panel->showTab(t);
            CHECK(panel->currentTab() == t);
            auto img = snapshot(*panel, "settings_panel_tab" + juce::String(t));
            CHECK(distinctColours(img) > 15);
        }
        bool closed = false;
        panel->onClose = [&closed] { closed = true; };
        CHECK(panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        CHECK(closed);
    }
    auto again = std::make_unique<ui::SettingsPanel>(processor);
    again->showTab(99); // clamped
    CHECK(again->currentTab() == ui::SettingsPanel::numTabs - 1);
}

TEST_CASE("Settings panel changes apply to the shared settings", "[gui][settings]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    auto& settings = processor.getSettings();

    auto s = settings.get();
    s.style = ui::Style::paperLight;
    s.layout = ui::Layout::compact;
    s.zoomPercent = 125;
    s.showCpu = false;
    settings.set(s);
    CHECK(editor.isCompact());
    CHECK(editor.getUiOptions().style == ui::Style::paperLight);
    CHECK(editor.getUiOptions().zoomPercent == 125);
    CHECK_FALSE(editor.getUiOptions().showCpu);
    CHECK(editor.getHeight() == ui::kCompactHeight);

    // Reset through the panel's manager goes back to defaults live.
    settings.resetToDefaults();
    CHECK_FALSE(editor.isCompact());
    CHECK(editor.getUiOptions().zoomPercent == 100);
    CHECK(editor.getHeight() == ui::kFullHeight);
}

TEST_CASE("Debug info contains the support details", "[gui][settings]")
{
    PentagritAudioProcessor processor;
    processor.setRateAndBufferSizeDetails(48000.0, 256);
    processor.prepareToPlay(48000.0, 256);
    processor.getMeters().latencySamples.store(480);
    const auto info = ui::SettingsPanel::buildDebugInfo(processor);
    INFO(info);
    CHECK(info.startsWithIgnoreCase("Pentagrit v"));
    CHECK(info.contains("OS: "));
    CHECK(info.contains("Host: "));
    CHECK(info.contains("Sample rate: 48000 Hz"));
    CHECK(info.contains("Block size: 256"));
    CHECK(info.contains("Latency: 480 samples"));
    CHECK(info.contains("Oversampling: "));
}

TEST_CASE("Editor with preset bar and settings overlay", "[gui][settings][Editor]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    editor.refreshNow();

    REQUIRE(editor.getPresetBar() != nullptr);
    CHECK(editor.getPresetBar()->getParentComponent() == &editor.presetSlot());
    CHECK(editor.presetSlot().getWidth() >= 300); // fits at the 1100 px base width
    CHECK(editor.getPresetBar()->getWidth() == editor.presetSlot().getWidth());
    CHECK(editor.getPresetBar()->isVisible());

    auto before = snapshot(editor, "editor_full_presetbar");
    CHECK(distinctColours(before) > 30);

    CHECK_FALSE(editor.isSettingsOpen());
    editor.getTopBar().settingsButton().onClick();
    REQUIRE(editor.isSettingsOpen());
    CHECK(editor.getSettingsPanel()->getBounds() == editor.content().getLocalBounds());
    for (int t = 0; t < ui::SettingsPanel::numTabs; ++t)
    {
        editor.getSettingsPanel()->showTab(t);
        auto img = snapshot(editor, "editor_settings_tab" + juce::String(t));
        CHECK(distinctColours(img) > 30);
        // the overlay dims the editor: the corner is no longer the plain ground colour
        CHECK(img.getPixelAt(1, 1) != ui::palette(ui::Style::studioDark).bg0);
    }

    // The gear toggles; Done / Esc close.
    editor.getTopBar().settingsButton().onClick();
    CHECK_FALSE(editor.isSettingsOpen());
    editor.openSettings();
    CHECK(editor.getSettingsPanel()->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(editor.isSettingsOpen());

    // Preset messages become footer toasts.
    editor.getPresetBar()->onMessage("Saved \"X\"");
    CHECK(editor.getFooter().hint().contains("Saved"));

    // Light style and compact layout with the overlay open.
    auto s = processor.getSettings().get();
    s.style = ui::Style::paperLight;
    s.layout = ui::Layout::compact;
    processor.getSettings().set(s);
    editor.openSettings();
    CHECK(editor.getSettingsPanel()->getBounds() == editor.content().getLocalBounds());
    CHECK(distinctColours(snapshot(editor, "editor_settings_light_compact")) > 30);
    processor.getSettings().resetToDefaults();
}

namespace
{
double luminance(juce::Colour c)
{
    auto lin = [](float v) { return v <= 0.03928f ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.getFloatRed()) + 0.7152 * lin(c.getFloatGreen()) + 0.0722 * lin(c.getFloatBlue());
}
double contrast(juce::Colour a, juce::Colour b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
} // namespace

TEST_CASE("Every style round trips through the settings file", "[settings][styles]")
{
    for (int i = 0; i < ui::kNumStyles; ++i)
    {
        const auto st = static_cast<ui::Style>(i);
        INFO(ui::styleId(st));
        TempFile tmp;
        {
            SettingsManager m(tmp.file);
            auto s = m.get();
            s.style = st;
            m.set(s);
        }
        SettingsManager m(tmp.file);
        CHECK(m.get().style == st);
        CHECK(SettingsManager::toUiOptions(m.get()).style == st);
    }
}

TEST_CASE("Legacy theme values migrate to styles", "[settings][styles]")
{
    auto load = [](const char* value)
    {
        TempFile tmp;
        tmp.file.replaceWithText(juce::String("<PROPERTIES><VALUE name=\"uiTheme\" val=\"") + value +
                                 "\"/></PROPERTIES>");
        SettingsManager m(tmp.file);
        return m.get().style;
    };
    CHECK(load("dark") == ui::Style::studioDark);
    CHECK(load("light") == ui::Style::paperLight);
    CHECK(load("purple") == ui::Style::studioDark);
    CHECK(load("") == ui::Style::studioDark);
}

TEST_CASE("Style palettes are legible", "[styles]")
{
    for (int i = 0; i < ui::kNumStyles; ++i)
    {
        const auto st = static_cast<ui::Style>(i);
        INFO(ui::styleName(st));
        const auto& p = ui::palette(st);
        CHECK(contrast(p.ink, p.bg1) >= 4.5);
        CHECK(contrast(p.ink2, p.bg1) >= 3.0);
        CHECK(contrast(p.onAct, p.act) >= 4.5);
        for (auto c : {p.mute, p.solo, p.bypass})
            CHECK(contrast(p.onBand, c) >= 4.5);
        // identifiers are stable and unique
        CHECK(ui::styleFromId(ui::styleId(st)) == st);
    }
}
