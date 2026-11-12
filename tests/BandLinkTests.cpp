// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pentagrit;
using Catch::Approx;

namespace
{
juce::AudioProcessorParameter& par(PentagritAudioProcessor& p, int band, BandParam bp)
{
    return *p.getAPVTS().getParameter(bandParamID(band, bp));
}

float val(PentagritAudioProcessor& p, int band, BandParam bp)
{
    return par(p, band, bp).getValue();
}

// A change without a gesture, as host automation or a preset does.
void automate(PentagritAudioProcessor& p, int band, BandParam bp, float normalised)
{
    par(p, band, bp).setValueNotifyingHost(normalised);
}

void setPlain(PentagritAudioProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getAPVTS().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

struct Fixture
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor{processor};

    explicit Fixture(int bands = 5)
    {
        setPlain(processor, pid::numBands, static_cast<float>(bands));
        editor.refreshNow();
        // A neutral, distinct starting point (written without gestures: nothing is linked yet).
        for (int b = 0; b < kMaxBands; ++b)
        {
            for (auto bp : {BandParam::gain, BandParam::attack, BandParam::sustain, BandParam::speed,
                            BandParam::drive, BandParam::mix})
            {
                automate(processor, b, bp, 0.5f);
            }
        }
        processor.clearUndoHistory();
    }

    void link(std::initializer_list<int> bands)
    {
        for (int b : bands)
        {
            editor.getBandLink().setLinked(b, true);
        }
    }

    // One UI drag: gesture start, the given values, gesture end.
    void drag(int band, BandParam bp, std::initializer_list<float> values)
    {
        auto& p = par(processor, band, bp);
        p.beginChangeGesture();
        for (float v : values)
        {
            p.setValueNotifyingHost(v);
        }
        p.endChangeGesture();
    }
};
} // namespace

TEST_CASE("Band link: siblings follow by the same normalised offset, clamped, without drift", "[bandlink]")
{
    Fixture f(3);
    automate(f.processor, 1, BandParam::attack, 0.6f);
    automate(f.processor, 2, BandParam::attack, 0.9f);
    f.link({0, 1, 2});

    auto& lead = par(f.processor, 0, BandParam::attack);
    lead.beginChangeGesture();
    lead.setValueNotifyingHost(0.7f); // +0.2
    CHECK(val(f.processor, 1, BandParam::attack) == Approx(0.8f).margin(1e-4f));
    CHECK(val(f.processor, 2, BandParam::attack) == Approx(1.0f).margin(1e-4f)); // clamped
    lead.setValueNotifyingHost(0.5f);                                            // back to the start
    CHECK(val(f.processor, 1, BandParam::attack) == Approx(0.6f).margin(1e-4f));
    CHECK(val(f.processor, 2, BandParam::attack) == Approx(0.9f).margin(1e-4f)); // no drift after clamping
    lead.setValueNotifyingHost(0.2f);                                            // -0.3
    CHECK(val(f.processor, 1, BandParam::attack) == Approx(0.3f).margin(1e-4f));
    CHECK(val(f.processor, 2, BandParam::attack) == Approx(0.6f).margin(1e-4f));
    lead.endChangeGesture();

    // Every continuous parameter is linked.
    for (auto bp : {BandParam::gain, BandParam::sustain, BandParam::speed, BandParam::drive, BandParam::mix})
    {
        f.drag(2, bp, {0.6f});
        CHECK(val(f.processor, 0, bp) == Approx(0.6f).margin(1e-4f));
        CHECK(val(f.processor, 1, bp) == Approx(0.6f).margin(1e-4f));
    }
    CHECK_FALSE(f.editor.getBandLink().isGestureOpen());
}

TEST_CASE("Band link: the saturation type is copied as is", "[bandlink]")
{
    Fixture f(3);
    f.link({0, 1, 2});
    automate(f.processor, 0, BandParam::satType, 0.0f);
    automate(f.processor, 1, BandParam::satType, 0.0f);
    automate(f.processor, 2, BandParam::satType, 0.0f);

    const float target = f.processor.getAPVTS().getParameter(bandParamID(0, BandParam::satType))->convertTo0to1(3.0f);
    f.drag(0, BandParam::satType, {target});
    CHECK(val(f.processor, 0, BandParam::satType) == Approx(target));
    CHECK(val(f.processor, 1, BandParam::satType) == Approx(target));
    CHECK(val(f.processor, 2, BandParam::satType) == Approx(target));
}

TEST_CASE("Band link: mute, solo and bypass are never linked", "[bandlink]")
{
    Fixture f(3);
    f.link({0, 1, 2});
    for (auto bp : {BandParam::mute, BandParam::solo, BandParam::bypass})
    {
        auto& p = par(f.processor, 0, bp);
        p.beginChangeGesture();
        p.setValueNotifyingHost(1.0f);
        p.endChangeGesture();
        CHECK(val(f.processor, 0, bp) == Approx(1.0f));
        CHECK(val(f.processor, 1, bp) == Approx(0.0f));
        CHECK(val(f.processor, 2, bp) == Approx(0.0f));
    }
}

TEST_CASE("Band link: unlinked and inactive bands stay put", "[bandlink]")
{
    Fixture f(3);
    f.link({0, 1, 3}); // band 3 is inactive with 3 bands, band 2 is not linked
    f.drag(0, BandParam::gain, {0.8f});
    CHECK(val(f.processor, 1, BandParam::gain) == Approx(0.8f).margin(1e-4f));
    CHECK(val(f.processor, 2, BandParam::gain) == Approx(0.5f));
    CHECK(val(f.processor, 3, BandParam::gain) == Approx(0.5f));
    CHECK(val(f.processor, 4, BandParam::gain) == Approx(0.5f));

    // An unlinked leader moves alone.
    f.drag(2, BandParam::gain, {0.1f});
    CHECK(val(f.processor, 0, BandParam::gain) == Approx(0.8f).margin(1e-4f));
    CHECK(val(f.processor, 1, BandParam::gain) == Approx(0.8f).margin(1e-4f));

    // An inactive leader does not drag the active ones either.
    f.drag(3, BandParam::gain, {0.2f});
    CHECK(val(f.processor, 0, BandParam::gain) == Approx(0.8f).margin(1e-4f));
}

TEST_CASE("Band link: a change without a gesture does not propagate", "[bandlink]")
{
    Fixture f(3);
    f.link({0, 1, 2});
    automate(f.processor, 0, BandParam::attack, 0.9f); // host automation
    CHECK(val(f.processor, 1, BandParam::attack) == Approx(0.5f));
    CHECK(val(f.processor, 2, BandParam::attack) == Approx(0.5f));
    automate(f.processor, 0, BandParam::satType, 1.0f);
    CHECK(val(f.processor, 1, BandParam::satType) != Approx(1.0f));
}

TEST_CASE("Band link: one undo step restores every band, redo reapplies", "[bandlink][undo]")
{
    Fixture f(3);
    f.link({0, 1, 2});
    auto& um = f.processor.getUndoManager();
    CHECK_FALSE(um.canUndo());

    f.drag(1, BandParam::sustain, {0.6f, 0.7f, 0.8f});
    for (int b = 0; b < 3; ++b)
    {
        CHECK(val(f.processor, b, BandParam::sustain) == Approx(0.8f).margin(1e-4f));
    }

    CHECK(f.editor.undo());
    for (int b = 0; b < 3; ++b)
    {
        CHECK(val(f.processor, b, BandParam::sustain) == Approx(0.5f).margin(1e-4f));
    }
    CHECK_FALSE(um.canUndo()); // it was a single step

    CHECK(f.editor.redo());
    for (int b = 0; b < 3; ++b)
    {
        CHECK(val(f.processor, b, BandParam::sustain) == Approx(0.8f).margin(1e-4f));
    }
    CHECK_FALSE(um.canRedo());
}

TEST_CASE("Band link: the mask is UI state kept by presets, A/B and the saved state", "[bandlink][state]")
{
    Fixture f(3);
    f.link({0, 2});
    auto& link = f.editor.getBandLink();
    CHECK(link.mask() == 0b101);
    CHECK(link.isLinked(0));
    CHECK_FALSE(link.isLinked(1));

    // Changing the mask is not an undo step.
    CHECK_FALSE(f.processor.getUndoManager().canUndo());

    // Preset load and A/B keep it.
    auto& pm = f.processor.getPresetManager();
    REQUIRE_FALSE(pm.getFactoryPresets().empty());
    CHECK(pm.loadPreset(pm.getFactoryPresets().front()).wasOk());
    CHECK(link.mask() == 0b101);
    pm.selectSlot(PresetManager::Slot::B);
    CHECK(link.mask() == 0b101);
    pm.selectSlot(PresetManager::Slot::A);
    CHECK(link.mask() == 0b101);

    // The editor shows it.
    f.editor.refreshNow();
    CHECK(f.editor.getStrip(0).isLinked());
    CHECK_FALSE(f.editor.getStrip(1).isLinked());
    CHECK(f.editor.getStrip(2).isLinked());

    // Round trip through the saved state.
    juce::MemoryBlock blob;
    f.processor.getStateInformation(blob);
    PentagritAudioProcessor other;
    other.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    CHECK(static_cast<int>(other.getAPVTS().state.getProperty(ui::BandLink::maskProperty, 0)) == 0b101);

    // A state without the property means nothing is linked.
    PentagritAudioProcessor fresh;
    CHECK(static_cast<int>(fresh.getAPVTS().state.getProperty(ui::BandLink::maskProperty, 0)) == 0);
}

TEST_CASE("Band link: the header toggle does not overlap M/S/B or the label at 2 to 5 bands",
          "[gui][bandlink]")
{
    Fixture f;
    for (int bands : {2, 3, 4, 5})
    {
        for (int si = 0; si < ui::kNumStyles; ++si)
        {
            for (auto layout : {ui::Layout::full, ui::Layout::compact})
            {
                setPlain(f.processor, pid::numBands, static_cast<float>(bands));
                ui::UiOptions o;
                o.style = static_cast<ui::Style>(si);
                o.layout = layout;
                f.editor.setUiOptions(o);
                f.editor.refreshNow();

                for (int b = 0; b < bands; ++b)
                {
                    INFO("style " << si << ", " << bands << " bands, band " << b
                                  << (layout == ui::Layout::compact ? ", compact" : ", full"));
                    auto& strip = f.editor.getStrip(b);
                    REQUIRE(strip.isVisible());
                    const auto btn = strip.headerButtonBounds();
                    for (size_t i = 0; i < btn.size(); ++i)
                    {
                        CHECK_FALSE(btn[i].isEmpty());
                        CHECK(strip.getLocalBounds().contains(btn[i]));
                        CHECK(btn[i].getX() >= ui::BandStrip::kHeaderLabelRight);
                        for (size_t j = i + 1; j < btn.size(); ++j)
                        {
                            CHECK_FALSE(btn[i].intersects(btn[j]));
                        }
                    }
                    // Same row for all four.
                    CHECK(btn[0].getY() == btn[1].getY());
                }
            }
        }
    }
}
