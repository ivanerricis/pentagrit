// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "presets/PresetManager.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace pentagrit;
using Catch::Approx;

namespace
{
struct TempDir
{
    TempDir()
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("pentagrit_test_" + juce::Uuid().toString()))
    {
        dir.createDirectory();
    }
    ~TempDir() { dir.deleteRecursively(); }
    juce::File dir;
};

struct Fixture
{
    TempDir tmp;
    PentagritAudioProcessor proc;
    PresetManager pm{proc.getAPVTS(), tmp.dir.getChildFile("presets")};

    void set(const juce::String& id, float v)
    {
        auto* p = proc.getAPVTS().getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(v));
    }
    float get(const juce::String& id) { return proc.getAPVTS().getRawParameterValue(id)->load(); }
};

PresetEntry factoryEntry(PresetManager& pm, const juce::String& name)
{
    for (const auto& e : pm.getFactoryPresets())
        if (e.name == name)
            return e;
    INFO("factory preset not found: " << name.toStdString());
    REQUIRE(false);
    return {};
}

struct CountingListener : juce::ChangeListener
{
    int count = 0;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
};

void writeText(const juce::File& f, const juce::String& text)
{
    f.getParentDirectory().createDirectory();
    f.replaceWithText(text);
}
} // namespace

TEST_CASE("Factory presets: the 13 presets are embedded and grouped", "[Preset]")
{
    Fixture f;
    const auto& list = f.pm.getFactoryPresets();
    REQUIRE(list.size() == 13);
    CHECK(f.pm.getFactoryCategories() == juce::StringArray{"Init", "Drums", "Bass", "Mix Bus", "Creative"});
    CHECK(list.front().name == "Init");
    CHECK(f.pm.getPresetsInCategory(true, "Drums").size() == 4);
    CHECK(f.pm.getPresetsInCategory(true, "Creative").size() == 4);
}

TEST_CASE("Factory presets: load with parameters in range and render without NaN", "[Preset]")
{
    Fixture f;
    f.proc.prepareToPlay(48000.0, 512);

    juce::Random rng(1234);
    for (const auto& e : f.pm.getFactoryPresets())
    {
        INFO(e.name.toStdString());
        REQUIRE(f.pm.loadPreset(e).wasOk());
        CHECK_FALSE(f.pm.isModified());
        CHECK(f.pm.getCurrentName() == e.name);

        const int bands = juce::roundToInt(f.get(pid::numBands));
        CHECK(bands >= 2);
        CHECK(bands <= kMaxBands);

        for (auto* p : f.proc.getParameters())
        {
            auto* r = dynamic_cast<juce::RangedAudioParameter*>(p);
            REQUIRE(r != nullptr);
            const float v = f.get(r->paramID);
            const auto range = r->getNormalisableRange();
            CHECK(std::isfinite(v));
            CHECK(v >= range.start);
            CHECK(v <= range.end);
        }
        for (int i = 1; i < kNumXovers; ++i)
            if (i < bands - 1)
                CHECK(f.get(pid::xover[static_cast<size_t>(i)]) >
                      f.get(pid::xover[static_cast<size_t>(i - 1)]));

        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        for (int block = 0; block < 12; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample(ch, i, block < 6 ? (rng.nextFloat() * 2.0f - 1.0f) * 0.5f : 0.0f);
            f.proc.processBlock(buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    REQUIRE(std::isfinite(buffer.getSample(ch, i)));
        }
    }
}

TEST_CASE("Factory presets set specific values", "[Preset]")
{
    Fixture f;
    REQUIRE(f.pm.loadPreset(factoryEntry(f.pm, "Full Destroy")).wasOk());
    CHECK(f.get(pid::numBands) == Approx(5.0f));
    CHECK(f.get(pid::oversampling) == Approx(3.0f));
    CHECK(f.get(pid::autoGain) == Approx(1.0f));
    CHECK(f.get(bandParamID(0, BandParam::satType)) == Approx(5.0f));

    REQUIRE(f.pm.loadPreset(factoryEntry(f.pm, "Init")).wasOk());
    CHECK(f.get(pid::numBands) == Approx(3.0f));
    CHECK(f.get(bandParamID(0, BandParam::satType)) == Approx(0.0f).margin(1e-5f));
    CHECK(f.get(pid::autoGain) == Approx(0.0f).margin(1e-5f));
    CHECK(f.get(pid::oversampling) == Approx(1.0f));
}

TEST_CASE("Preset round trip: saveAs then load", "[Preset]")
{
    Fixture f;
    f.set(pid::inGain, -6.5f);
    f.set(pid::xover[1], 750.0f);
    f.set(bandParamID(2, BandParam::satType), 4.0f);
    f.set(bandParamID(1, BandParam::mute), 1.0f);

    REQUIRE(f.pm.saveAs("My Preset", "Mine").wasOk());
    CHECK(f.pm.getCurrentName() == "My Preset");
    CHECK(f.pm.getCurrentCategory() == "Mine");
    CHECK_FALSE(f.pm.isCurrentFactory());
    CHECK_FALSE(f.pm.isModified());
    CHECK(f.tmp.dir.getChildFile("presets/Mine/My Preset.pgp").existsAsFile());

    REQUIRE(f.pm.loadInit().wasOk());
    CHECK(f.get(pid::inGain) == Approx(0.0f).margin(1e-5f));

    auto user = f.pm.getUserPresets();
    REQUIRE(user.size() == 1);
    REQUIRE(f.pm.loadPreset(user[0]).wasOk());
    CHECK(f.get(pid::inGain) == Approx(-6.5f).margin(0.05f));
    CHECK(f.get(pid::xover[1]) == Approx(750.0f).epsilon(1e-3));
    CHECK(f.get(bandParamID(2, BandParam::satType)) == Approx(4.0f));
    CHECK(f.get(bandParamID(1, BandParam::mute)) == Approx(1.0f));
    CHECK_FALSE(f.pm.isModified());

    // The file is the documented format.
    auto xml = juce::XmlDocument::parse(f.tmp.dir.getChildFile("presets/Mine/My Preset.pgp"));
    REQUIRE(xml != nullptr);
    CHECK(xml->hasTagName("PENTAGRIT_PRESET"));
    CHECK(xml->getIntAttribute("stateVersion") == kStateVersion);
    CHECK(xml->getStringAttribute("name") == "My Preset");
    CHECK(xml->getStringAttribute("category") == "Mine");
}

TEST_CASE("Preset names are sanitised and invalid names rejected", "[Preset]")
{
    CHECK(PresetManager::sanitiseName("  Hello   World  ") == "Hello World");
    CHECK(PresetManager::sanitiseName("a/b\\c:d*e?f\"g<h>i|j") == "abcdefghij");
    CHECK(PresetManager::sanitiseName("name...") == "name");
    CHECK(PresetManager::sanitiseName("").isEmpty());
    CHECK(PresetManager::sanitiseName("   ").isEmpty());
    CHECK(PresetManager::sanitiseName("..").isEmpty());
    CHECK(PresetManager::sanitiseName("CON").isEmpty());
    CHECK(PresetManager::sanitiseName("com1").isEmpty());
    CHECK(PresetManager::sanitiseName("///").isEmpty());
    CHECK(PresetManager::sanitiseName(juce::String::repeatedString("x", 200)).length() == 64);

    Fixture f;
    CHECK(f.pm.saveAs("", "Cat").failed());
    CHECK(f.pm.saveAs("???", "Cat").failed());
    CHECK(f.pm.saveAs("Ok", "..").failed());        // path traversal through the category
    CHECK(f.pm.saveAs("Ok", "../../evil").wasOk()); // separators are stripped, stays inside the root
    for (const auto& e : f.pm.getUserPresets())
        CHECK(e.file.isAChildOf(f.tmp.dir.getChildFile("presets")));
    CHECK(f.pm.saveAs("Blank category", "   ").wasOk());
    CHECK(f.pm.getCurrentCategory() == "Unsorted");
}

TEST_CASE("Preset: no accidental overwrite", "[Preset]")
{
    Fixture f;
    REQUIRE(f.pm.saveAs("Keep", "Cat").wasOk());
    f.set(pid::inGain, 5.0f);
    REQUIRE(f.pm.loadInit().wasOk());
    f.set(pid::inGain, -5.0f);

    CHECK(f.pm.userPresetExists("Keep", "Cat"));
    CHECK(f.pm.saveAs("Keep", "Cat").failed()); // different current preset: refuse
    auto entries = f.pm.getUserPresets();
    REQUIRE(entries.size() == 1);

    REQUIRE(f.pm.saveAs("Keep", "Cat", true).wasOk()); // explicit overwrite
    REQUIRE(f.pm.loadInit().wasOk());
    REQUIRE(f.pm.loadPreset(f.pm.getUserPresets()[0]).wasOk());
    CHECK(f.get(pid::inGain) == Approx(-5.0f).margin(0.05f));

    // saveAs on the current preset itself is allowed and save() overwrites it.
    f.set(pid::inGain, 3.0f);
    CHECK(f.pm.isModified());
    REQUIRE(f.pm.save().wasOk());
    CHECK_FALSE(f.pm.isModified());
}

TEST_CASE("Factory presets are read-only", "[Preset]")
{
    Fixture f;
    REQUIRE(f.pm.loadPreset(factoryEntry(f.pm, "Bass Grit")).wasOk());
    CHECK(f.pm.isCurrentFactory());
    CHECK(f.pm.save().failed());
    CHECK(f.pm.rename("Other").failed());
    CHECK(f.pm.deleteCurrent().failed());
}

TEST_CASE("Corrupt and garbage preset files are ignored", "[Preset]")
{
    Fixture f;
    const auto root = f.tmp.dir.getChildFile("presets");
    REQUIRE(f.pm.saveAs("Good", "Cat").wasOk());

    writeText(root.getChildFile("Cat/garbage.pgp"), "this is not xml at all \x01\x02");
    writeText(root.getChildFile("Cat/empty.pgp"), "");
    writeText(root.getChildFile("Cat/wrongtag.pgp"), "<SOMETHING_ELSE/>");
    writeText(root.getChildFile("Cat/nostate.pgp"), "<PENTAGRIT_PRESET stateVersion=\"1\" name=\"x\"/>");
    writeText(root.getChildFile("Cat/truncated.pgp"),
              "<PENTAGRIT_PRESET stateVersion=\"1\" name=\"x\"><PENTAGRIT_STA");
    writeText(root.getChildFile("Cat/notpreset.txt"), "ignored");

    f.pm.rescan();
    REQUIRE(f.pm.getUserPresets().size() == 1);
    CHECK(f.pm.getUserPresets()[0].name == "Good");
    CHECK(f.pm.getSkippedFiles().size() == 5);

    PresetEntry bogus;
    bogus.name = "garbage";
    bogus.category = "Cat";
    bogus.file = root.getChildFile("Cat/garbage.pgp");
    f.set(pid::inGain, 4.0f);
    CHECK(f.pm.loadPreset(bogus).failed());
    CHECK(f.get(pid::inGain) == Approx(4.0f).margin(0.05f)); // untouched
}

TEST_CASE("Preset stateVersion handling", "[Preset]")
{
    PresetEntry meta;
    PresetManager::ParamValues values;
    auto parse = [&](const juce::String& xmlText)
    {
        auto xml = juce::XmlDocument::parse(xmlText);
        REQUIRE(xml != nullptr);
        return PresetManager::parsePreset(*xml, meta, values);
    };

    const juce::String state = "<PENTAGRIT_STATE><PARAM id=\"inGain\" value=\"3\"/></PENTAGRIT_STATE>";
    CHECK(parse("<PENTAGRIT_PRESET stateVersion=\"1\" name=\"a\">" + state + "</PENTAGRIT_PRESET>").wasOk());
    CHECK(values.at("inGain") == Approx(3.0f));
    CHECK(meta.name == "a");
    // A missing version is read as version 1.
    CHECK(parse("<PENTAGRIT_PRESET name=\"a\">" + state + "</PENTAGRIT_PRESET>").wasOk());
    // Newer, zero, negative and non-numeric versions are rejected.
    CHECK(
        parse("<PENTAGRIT_PRESET stateVersion=\"99\" name=\"a\">" + state + "</PENTAGRIT_PRESET>").failed());
    CHECK(parse("<PENTAGRIT_PRESET stateVersion=\"0\" name=\"a\">" + state + "</PENTAGRIT_PRESET>").failed());
    CHECK(
        parse("<PENTAGRIT_PRESET stateVersion=\"-1\" name=\"a\">" + state + "</PENTAGRIT_PRESET>").failed());
    CHECK(
        parse("<PENTAGRIT_PRESET stateVersion=\"abc\" name=\"a\">" + state + "</PENTAGRIT_PRESET>").failed());

    // Excluded parameters are dropped while parsing.
    CHECK(parse("<PENTAGRIT_PRESET stateVersion=\"1\" name=\"a\"><PENTAGRIT_STATE>"
                "<PARAM id=\"osPhase\" value=\"1\"/><PARAM id=\"osRender\" value=\"2\"/>"
                "<PARAM id=\"bypass\" value=\"1\"/><PARAM id=\"mix\" "
                "value=\"0.5\"/></PENTAGRIT_STATE></PENTAGRIT_PRESET>")
              .wasOk());
    CHECK(values.size() == 1);
    CHECK(values.count("mix") == 1);

    // A newer preset on disk is skipped by the scan.
    Fixture f;
    writeText(f.tmp.dir.getChildFile("presets/Cat/new.pgp"),
              "<PENTAGRIT_PRESET stateVersion=\"99\" name=\"new\">" + state + "</PENTAGRIT_PRESET>");
    f.pm.rescan();
    CHECK(f.pm.getUserPresets().empty());
}

TEST_CASE("Preset values are clamped and missing parameters use defaults", "[Preset]")
{
    Fixture f;
    f.set(pid::outGain, 6.0f);
    const auto file = f.tmp.dir.getChildFile("presets/Cat/wild.pgp");
    writeText(file, "<PENTAGRIT_PRESET stateVersion=\"1\" name=\"wild\" category=\"Cat\"><PENTAGRIT_STATE>"
                    "<PARAM id=\"inGain\" value=\"999\"/><PARAM id=\"mix\" value=\"-3\"/>"
                    "<PARAM id=\"numBands\" value=\"9\"/><PARAM id=\"b1_satType\" value=\"nan\"/>"
                    "<PARAM id=\"unknownParam\" value=\"1\"/></PENTAGRIT_STATE></PENTAGRIT_PRESET>");
    f.pm.rescan();
    REQUIRE(f.pm.getUserPresets().size() == 1);
    REQUIRE(f.pm.loadPreset(f.pm.getUserPresets()[0]).wasOk());
    CHECK(f.get(pid::inGain) == Approx(24.0f));
    CHECK(f.get(pid::mix) == Approx(0.0f).margin(1e-5f));
    CHECK(f.get(pid::numBands) == Approx(5.0f));
    CHECK(f.get(bandParamID(0, BandParam::satType)) == Approx(0.0f).margin(1e-5f));
    CHECK(f.get(pid::outGain) == Approx(0.0f).margin(1e-5f)); // not listed: default
}

TEST_CASE("osPhase, osRender and bypass survive preset loads", "[Preset]")
{
    Fixture f;
    f.set(pid::osPhase, 1.0f);
    f.set(pid::osRender, 3.0f);
    f.set(pid::bypass, 1.0f);

    for (const auto& e : f.pm.getFactoryPresets())
    {
        REQUIRE(f.pm.loadPreset(e).wasOk());
        CHECK(f.get(pid::osPhase) == Approx(1.0f));
        CHECK(f.get(pid::osRender) == Approx(3.0f));
        CHECK(f.get(pid::bypass) == Approx(1.0f));
    }

    // Also when a user file tries to set them.
    const auto file = f.tmp.dir.getChildFile("presets/Cat/sneaky.pgp");
    writeText(file, "<PENTAGRIT_PRESET stateVersion=\"1\" name=\"sneaky\"><PENTAGRIT_STATE>"
                    "<PARAM id=\"osPhase\" value=\"0\"/><PARAM id=\"osRender\" "
                    "value=\"0\"/></PENTAGRIT_STATE></PENTAGRIT_PRESET>");
    f.pm.rescan();
    REQUIRE(f.pm.loadPreset(f.pm.getUserPresets()[0]).wasOk());
    CHECK(f.get(pid::osPhase) == Approx(1.0f));
    CHECK(f.get(pid::osRender) == Approx(3.0f));

    // The UI size stored in the state is kept as well.
    f.proc.setUiSize(1000, 640);
    REQUIRE(f.pm.loadPreset(factoryEntry(f.pm, "Init")).wasOk());
    CHECK(f.proc.getUiSize().x == 1000);

    // And presets written by the manager do not contain them.
    REQUIRE(f.pm.saveAs("Written", "Cat").wasOk());
    auto xml = juce::XmlDocument::parse(f.tmp.dir.getChildFile("presets/Cat/Written.pgp"));
    REQUIRE(xml != nullptr);
    for (auto* p : xml->getChildByName("PENTAGRIT_STATE")->getChildWithTagNameIterator("PARAM"))
        CHECK(PresetManager::isPresetParameter(p->getStringAttribute("id")));
}

TEST_CASE("Dirty flag tracks parameter changes", "[Preset]")
{
    Fixture f;
    CHECK(f.pm.getCurrentName() == "Init");
    CHECK_FALSE(f.pm.isModified());
    CHECK(f.pm.getDisplayName() == "Init");

    CountingListener listener;
    f.pm.addChangeListener(&listener);

    f.set(pid::inGain, 3.0f);
    CHECK(f.pm.isModified());
    CHECK(f.pm.getDisplayName() == "Init *");
    CHECK(listener.count >= 1);

    const int before = listener.count;
    f.set(pid::inGain, 4.0f); // still modified: no flip, no notification
    CHECK(listener.count == before);

    f.set(pid::inGain, 0.0f);
    CHECK_FALSE(f.pm.isModified());
    CHECK(listener.count > before);

    // Excluded parameters never make the preset modified.
    f.set(pid::osPhase, 1.0f);
    f.set(pid::bypass, 1.0f);
    CHECK_FALSE(f.pm.isModified());

    f.set(bandParamID(1, BandParam::attack), 6.0f);
    CHECK(f.pm.isModified());
    REQUIRE(f.pm.loadPreset(factoryEntry(f.pm, "Snare Snap")).wasOk());
    CHECK_FALSE(f.pm.isModified());
    f.pm.removeChangeListener(&listener);
}

TEST_CASE("Current preset is stored in the APVTS state and restored", "[Preset]")
{
    Fixture a;
    REQUIRE(a.pm.loadPreset(factoryEntry(a.pm, "Sub Tight")).wasOk());
    a.set(pid::inGain, 2.0f);

    juce::MemoryBlock block;
    a.proc.getStateInformation(block);

    Fixture b;
    b.proc.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(b.pm.getCurrentName() == "Sub Tight");
    CHECK(b.pm.getCurrentCategory() == "Bass");
    CHECK(b.pm.isCurrentFactory());
    CHECK(b.pm.isModified()); // the restored state differs from the preset
    b.set(pid::inGain, 0.0f);
    CHECK_FALSE(b.pm.isModified());
}

TEST_CASE("Rename, duplicate and delete user presets", "[Preset]")
{
    Fixture f;
    const auto root = f.tmp.dir.getChildFile("presets");
    f.set(pid::inGain, 2.0f);
    REQUIRE(f.pm.saveAs("Alpha", "Cat").wasOk());

    SECTION("rename")
    {
        CHECK(f.pm.rename("").failed());
        REQUIRE(f.pm.rename("Beta").wasOk());
        CHECK(f.pm.getCurrentName() == "Beta");
        CHECK_FALSE(root.getChildFile("Cat/Alpha.pgp").exists());
        CHECK(root.getChildFile("Cat/Beta.pgp").existsAsFile());
        REQUIRE(f.pm.getUserPresets().size() == 1);
        CHECK(f.pm.getUserPresets()[0].name == "Beta");
        CHECK_FALSE(f.pm.isModified());

        REQUIRE(f.pm.duplicateCurrent().wasOk());
        CHECK(f.pm.rename("Beta").failed()); // collides with the original
        CHECK(root.getChildFile("Cat/Beta.pgp").existsAsFile());
    }

    SECTION("duplicate")
    {
        REQUIRE(f.pm.duplicateCurrent().wasOk());
        CHECK(f.pm.getCurrentName() == "Alpha copy");
        CHECK(f.pm.getCurrentCategory() == "Cat");
        REQUIRE(f.pm.duplicateCurrent().wasOk());
        CHECK(f.pm.getCurrentName() == "Alpha copy copy");
        REQUIRE(f.pm.loadPreset(f.pm.getPresetsInCategory(false, "Cat")[0]).wasOk());
        REQUIRE(f.pm.getUserPresets().size() == 3);

        // Duplicating a name that already exists adds a counter.
        REQUIRE(f.pm.loadPreset(factoryEntry(f.pm, "Init")).wasOk());
        REQUIRE(f.pm.duplicateCurrent().wasOk());
        CHECK(f.pm.getCurrentName() == "Init copy");
        CHECK(f.pm.getCurrentCategory() == "Unsorted");
        REQUIRE(f.pm.duplicateCurrent().wasOk());
        REQUIRE(f.pm.loadPreset(factoryEntry(f.pm, "Init")).wasOk());
        REQUIRE(f.pm.duplicateCurrent().wasOk());
        CHECK(f.pm.getCurrentName() == "Init copy 2");
    }

    SECTION("delete")
    {
        REQUIRE(f.pm.deleteCurrent().wasOk());
        CHECK_FALSE(root.getChildFile("Cat/Alpha.pgp").exists());
        CHECK_FALSE(root.getChildFile("Cat").exists()); // empty category folder removed
        CHECK(f.pm.getUserPresets().empty());
        CHECK(f.pm.getCurrentName() == "Init"); // falls back to Init
        CHECK(f.pm.isCurrentFactory());
        CHECK(f.get(pid::inGain) == Approx(0.0f).margin(1e-5f));
    }
}

TEST_CASE("Import and export", "[Preset]")
{
    Fixture f;
    f.set(pid::inGain, -9.0f);
    f.set(bandParamID(0, BandParam::drive), 20.0f);
    REQUIRE(f.pm.saveAs("Source", "Cat").wasOk());

    const auto exported = f.tmp.dir.getChildFile("out/shared"); // extension is added
    REQUIRE(f.pm.exportCurrent(exported).wasOk());
    const auto exportedFile = f.tmp.dir.getChildFile("out/shared.pgp");
    REQUIRE(exportedFile.existsAsFile());

    PresetEntry meta;
    PresetManager::ParamValues values;
    REQUIRE(PresetManager::parsePresetFile(exportedFile, meta, values).wasOk());
    CHECK(meta.name == "Source");

    Fixture g; // a fresh machine
    PresetEntry created;
    REQUIRE(g.pm.importPreset(exportedFile, &created).wasOk());
    CHECK(created.name == "Source");
    CHECK(created.category == "Cat");
    REQUIRE(g.pm.getUserPresets().size() == 1);

    // Importing again never overwrites.
    PresetEntry created2;
    REQUIRE(g.pm.importPreset(exportedFile, &created2).wasOk());
    CHECK(created2.name == "Source 2");
    CHECK(g.pm.getUserPresets().size() == 2);

    REQUIRE(g.pm.loadPreset(created).wasOk());
    CHECK(g.get(pid::inGain) == Approx(-9.0f).margin(0.05f));
    CHECK(g.get(bandParamID(0, BandParam::drive)) == Approx(20.0f).margin(0.05f));

    // Garbage is rejected and nothing is added.
    const auto bad = g.tmp.dir.getChildFile("bad.pgp");
    writeText(bad, "definitely not a preset");
    CHECK(g.pm.importPreset(bad).failed());
    CHECK(g.pm.importPreset(g.tmp.dir.getChildFile("missing.pgp")).failed());
    CHECK(g.pm.getUserPresets().size() == 2);

    // Export of a factory preset works too.
    REQUIRE(g.pm.loadPreset(factoryEntry(g.pm, "Punchy Kick")).wasOk());
    REQUIRE(g.pm.exportCurrent(g.tmp.dir.getChildFile("kick.pgp")).wasOk());
    REQUIRE(PresetManager::parsePresetFile(g.tmp.dir.getChildFile("kick.pgp"), meta, values).wasOk());
    CHECK(meta.name == "Punchy Kick");
}

TEST_CASE("Prev/next follow menu order and wrap", "[Preset]")
{
    Fixture f;
    f.set(pid::inGain, 1.0f);
    REQUIRE(f.pm.saveAs("Zed", "Beta").wasOk());
    REQUIRE(f.pm.saveAs("Ann", "Beta").wasOk());
    REQUIRE(f.pm.saveAs("Mid", "Alpha").wasOk());

    const auto order = f.pm.getOrderedPresets();
    REQUIRE(order.size() == 16);
    CHECK(order[0].name == "Init");
    CHECK(order[1].name == "Punchy Kick");
    CHECK(order[12].name == "Full Destroy");
    // User presets: categories alphabetical, names alphabetical.
    CHECK(order[13].name == "Mid");
    CHECK(order[14].name == "Ann");
    CHECK(order[15].name == "Zed");

    REQUIRE(f.pm.loadPreset(order[0]).wasOk());
    REQUIRE(f.pm.loadPrevious().wasOk()); // wrap to the last
    CHECK(f.pm.getCurrentName() == "Zed");
    CHECK_FALSE(f.pm.isCurrentFactory());
    REQUIRE(f.pm.loadNext().wasOk()); // wrap to the first
    CHECK(f.pm.getCurrentName() == "Init");

    for (size_t i = 1; i < order.size(); ++i)
    {
        REQUIRE(f.pm.loadNext().wasOk());
        CHECK(f.pm.getCurrentName() == order[i].name);
    }
}

TEST_CASE("A/B compare keeps two states", "[Preset]")
{
    Fixture f;
    CHECK(f.pm.getActiveSlot() == PresetManager::Slot::A);
    REQUIRE(f.pm.loadPreset(factoryEntry(f.pm, "Punchy Kick")).wasOk());
    f.set(pid::inGain, 3.0f); // state A: Punchy Kick + inGain 3

    f.pm.selectSlot(PresetManager::Slot::B);
    CHECK(f.pm.getActiveSlot() == PresetManager::Slot::B);
    CHECK(f.get(pid::inGain) == Approx(3.0f).margin(0.05f)); // B starts as a copy of A

    REQUIRE(f.pm.loadPreset(factoryEntry(f.pm, "Bass Grit")).wasOk());
    f.set(pid::outGain, -4.0f); // state B

    f.pm.selectSlot(PresetManager::Slot::A);
    CHECK(f.pm.getCurrentName() == "Punchy Kick");
    CHECK(f.pm.isModified());
    CHECK(f.get(pid::inGain) == Approx(3.0f).margin(0.05f));
    CHECK(f.get(pid::outGain) == Approx(0.0f).margin(0.05f));
    CHECK(f.get(pid::numBands) == Approx(3.0f));

    f.pm.selectSlot(PresetManager::Slot::B);
    CHECK(f.pm.getCurrentName() == "Bass Grit");
    CHECK(f.pm.isModified());
    CHECK(f.get(pid::outGain) == Approx(-4.0f).margin(0.05f));
    CHECK(f.get(pid::inGain) == Approx(0.0f).margin(0.05f));
    CHECK(f.get(bandParamID(1, BandParam::satType)) == Approx(3.0f));

    // Selecting the active slot again is a no-op.
    f.pm.selectSlot(PresetManager::Slot::B);
    CHECK(f.get(pid::outGain) == Approx(-4.0f).margin(0.05f));
}

TEST_CASE("setUserFolder re-scans and categories come from sub-folders", "[Preset]")
{
    Fixture f;
    REQUIRE(f.pm.saveAs("One", "Drums2").wasOk());
    REQUIRE(f.pm.saveAs("Root level", "").wasOk());
    CHECK(f.pm.getUserCategories() == juce::StringArray{"Drums2", "Unsorted"});

    const auto other = f.tmp.dir.getChildFile("other");
    CountingListener listener;
    f.pm.addChangeListener(&listener);
    f.pm.setUserFolder(other);
    CHECK(f.pm.getUserFolder() == other);
    CHECK(f.pm.getUserPresets().empty());
    CHECK(listener.count >= 1);
    f.pm.removeChangeListener(&listener);

    f.pm.setUserFolder(f.tmp.dir.getChildFile("presets"));
    CHECK(f.pm.getUserPresets().size() == 2);

    // A hand-made root-level file lands in Unsorted.
    writeText(f.tmp.dir.getChildFile("presets/manual.pgp"),
              "<PENTAGRIT_PRESET stateVersion=\"1\" name=\"Manual\"><PENTAGRIT_STATE/></PENTAGRIT_PRESET>");
    f.pm.rescan();
    CHECK(f.pm.getPresetsInCategory(false, "Unsorted").size() == 2);

    CHECK(PresetManager::getDefaultUserFolder().getFullPathName().contains("Pentagrit"));
}
