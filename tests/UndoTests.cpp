// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pentagrit;
using Catch::Approx;

namespace
{
float raw(PentagritAudioProcessor& p, const juce::String& id)
{
    return p.getAPVTS().getRawParameterValue(id)->load();
}

// What automation does: a value change without a gesture.
void automate(PentagritAudioProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getAPVTS().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

struct KnobFixture
{
    PentagritAudioProcessor processor;
    ui::LabelledKnob knob{
        processor.getAPVTS(), pid::inGain, "In", "hint", ui::LabelledKnob::Fmt::dbSignedUnit, true, 44};
    KnobFixture() { knob.setBounds(0, 0, 60, 80); }
    juce::UndoManager& um() { return processor.getUndoManager(); }
};
} // namespace

TEST_CASE("Undo: a typed value is one step", "[undo]")
{
    KnobFixture f;
    CHECK_FALSE(f.um().canUndo());
    CHECK_FALSE(f.um().canRedo());

    f.knob.commitText("4");
    CHECK(raw(f.processor, pid::inGain) == Approx(4.0f).margin(0.02f));
    CHECK(f.um().canUndo());
    CHECK_FALSE(f.um().canRedo());

    CHECK(f.um().undo());
    CHECK(raw(f.processor, pid::inGain) == Approx(0.0f).margin(1.0e-3f));
    CHECK_FALSE(f.um().canUndo());
    CHECK(f.um().canRedo());

    CHECK(f.um().redo());
    CHECK(raw(f.processor, pid::inGain) == Approx(4.0f).margin(0.02f));
    CHECK(f.um().canUndo());
    CHECK_FALSE(f.um().canRedo());
}

TEST_CASE("Undo: a drag is one step, whatever the number of value changes", "[undo]")
{
    KnobFixture f;
    auto& s = f.knob.slider();
    {
        juce::Slider::ScopedDragNotification drag(s);
        for (double v : {1.0, 2.5, 4.0, 6.0, 5.0})
            s.setValue(v, juce::sendNotificationSync);
    }
    CHECK(raw(f.processor, pid::inGain) == Approx(5.0f).margin(0.02f));

    CHECK(f.um().undo());
    CHECK(raw(f.processor, pid::inGain) == Approx(0.0f).margin(1.0e-3f));
    CHECK_FALSE(f.um().canUndo()); // no second step hiding behind it

    CHECK(f.um().redo());
    CHECK(raw(f.processor, pid::inGain) == Approx(5.0f).margin(0.02f));
}

TEST_CASE("Undo: consecutive gestures are separate steps, a click without a change is none", "[undo]")
{
    KnobFixture f;
    f.knob.commitText("2");
    f.knob.commitText("-3");
    {
        juce::Slider::ScopedDragNotification drag(f.knob.slider()); // touched, not moved
    }
    f.knob.commitText("-3"); // same value again
    CHECK(f.um().getUndoDescriptions().size() == 2);

    CHECK(f.um().undo());
    CHECK(raw(f.processor, pid::inGain) == Approx(2.0f).margin(0.02f));
    CHECK(f.um().undo());
    CHECK(raw(f.processor, pid::inGain) == Approx(0.0f).margin(1.0e-3f));
    CHECK_FALSE(f.um().undo());
}

TEST_CASE("Undo: a new change drops the redo steps", "[undo]")
{
    KnobFixture f;
    f.knob.commitText("2");
    f.um().undo();
    CHECK(f.um().canRedo());
    f.knob.commitText("5");
    CHECK_FALSE(f.um().canRedo());
}

TEST_CASE("Undo: toggles, selectors and crossover handles", "[undo]")
{
    PentagritAudioProcessor processor;
    auto& um = processor.getUndoManager();
    auto& apvts = processor.getAPVTS();

    // Toggle click (ButtonParameterAttachment: one complete gesture per click).
    juce::ToggleButton button;
    juce::ButtonParameterAttachment att(*apvts.getParameter(pid::bypass), button, nullptr);
    button.setToggleState(true, juce::sendNotificationSync); // what a click does
    CHECK(raw(processor, pid::bypass) > 0.5f);
    CHECK(um.undo());
    CHECK(raw(processor, pid::bypass) < 0.5f);
    CHECK(um.redo());
    CHECK(raw(processor, pid::bypass) > 0.5f);
    um.clearUndoHistory();

    // Selector.
    ui::SegmentedSelector sel(*apvts.getParameter(pid::oversampling),
                              juce::StringArray{"Off", "2x", "4x", "8x"}, 0, "OS", "hint");
    const float osBefore = raw(processor, pid::oversampling);
    sel.select(2);
    CHECK(raw(processor, pid::oversampling) != osBefore);
    CHECK(um.undo());
    CHECK(raw(processor, pid::oversampling) == Approx(osBefore));
    CHECK_FALSE(um.canUndo());
    um.clearUndoHistory();

    // Crossover handle: typed value, then reset.
    MeterState meters;
    ui::CrossoverDisplay display(apvts, meters);
    display.setBounds(0, 0, 800, 200);
    const float x0 = raw(processor, pid::xover[0]);
    display.commitHz(0, 200.0f);
    display.resetHandle(0);
    CHECK(um.getUndoDescriptions().size() == 2);
    CHECK(um.undo());
    CHECK(raw(processor, pid::xover[0]) == Approx(200.0f).margin(0.5f));
    CHECK(um.undo());
    CHECK(raw(processor, pid::xover[0]) == Approx(x0).margin(0.5f));
}

TEST_CASE("Undo: a band count change and the handles it pushes are one step", "[undo][gui]")
{
    PentagritAudioProcessor processor;
    automate(processor, pid::numBands, 2.0f);
    automate(processor, pid::xover[0], 1000.0f);
    automate(processor, pid::xover[1], 1050.0f);
    automate(processor, pid::xover[2], 1060.0f);
    PentagritAudioProcessorEditor editor(processor);
    auto& um = processor.getUndoManager();
    REQUIRE_FALSE(um.canUndo());

    const float x1 = raw(processor, pid::xover[1]);
    const float x2 = raw(processor, pid::xover[2]);

    juce::ParameterAttachment user(*processor.getAPVTS().getParameter(pid::numBands), [](float) {}, nullptr);
    user.setValueAsCompleteGesture(5.0f); // the selector
    CHECK(raw(processor, pid::numBands) == Approx(5.0f));
    CHECK(raw(processor, pid::xover[1]) > x1 * 1.1f); // pushed apart
    CHECK(um.getUndoDescriptions().size() == 1);

    CHECK(um.undo());
    editor.refreshNow();
    CHECK(raw(processor, pid::numBands) == Approx(2.0f));
    CHECK(raw(processor, pid::xover[1]) == Approx(x1).margin(0.5f));
    CHECK(raw(processor, pid::xover[2]) == Approx(x2).margin(0.5f));
    CHECK_FALSE(um.canUndo());
}

TEST_CASE("Undo: host automation is not recorded, not even the handles the editor pushes", "[undo][gui]")
{
    PentagritAudioProcessor processor;
    automate(processor, pid::numBands, 2.0f);
    automate(processor, pid::xover[0], 1000.0f);
    automate(processor, pid::xover[1], 1050.0f);
    PentagritAudioProcessorEditor editor(processor);
    auto& um = processor.getUndoManager();

    for (int i = 0; i < 50; ++i)
        automate(processor, pid::inGain, -12.0f + 0.2f * static_cast<float>(i));
    automate(processor, pid::numBands, 5.0f); // the editor pushes the handles from its callback
    editor.refreshNow();
    CHECK(raw(processor, pid::xover[1]) > 1050.0f);
    CHECK_FALSE(um.canUndo());
    CHECK(um.getNumberOfUnitsTakenUpByStoredCommands() == 0);
}

TEST_CASE("Undo: the history is bounded", "[undo]")
{
    KnobFixture f;
    for (int i = 0; i < 1000; ++i)
        f.knob.commitText(i % 2 == 0 ? "3" : "-3");
    CHECK(f.um().getNumberOfUnitsTakenUpByStoredCommands() <= PentagritAudioProcessor::kUndoMaxUnits + 1);
    CHECK(f.um().getUndoDescriptions().size() >= PentagritAudioProcessor::kUndoMinTransactions);
    CHECK(f.um().getUndoDescriptions().size() <= PentagritAudioProcessor::kUndoMaxUnits + 1);
}

TEST_CASE("Undo: preset load, A/B and state restore clear the history", "[undo][Preset]")
{
    KnobFixture f;
    auto& pm = f.processor.getPresetManager();
    auto& factory = pm.getFactoryPresets();
    REQUIRE(factory.size() > 1);

    f.knob.commitText("2");
    REQUIRE(f.um().canUndo());
    REQUIRE(pm.loadPreset(factory[1]).wasOk());
    CHECK_FALSE(f.um().canUndo());
    CHECK_FALSE(f.um().canRedo());

    f.knob.commitText("3");
    f.um().undo();
    REQUIRE(f.um().canRedo());
    REQUIRE(pm.loadInit().wasOk());
    CHECK_FALSE(f.um().canUndo());
    CHECK_FALSE(f.um().canRedo());

    f.knob.commitText("2");
    REQUIRE(f.um().canUndo());
    pm.selectSlot(PresetManager::Slot::B);
    CHECK_FALSE(f.um().canUndo());
    f.knob.commitText("1");
    REQUIRE(f.um().canUndo());
    pm.selectSlot(PresetManager::Slot::A);
    CHECK_FALSE(f.um().canUndo());

    f.knob.commitText("6");
    REQUIRE(f.um().canUndo());
    juce::MemoryBlock block;
    f.processor.getStateInformation(block);
    f.knob.commitText("5");
    f.processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK_FALSE(f.um().canUndo());
    CHECK_FALSE(f.um().canRedo());
    CHECK(raw(f.processor, pid::inGain) == Approx(6.0f).margin(0.02f));

    // Invalid data changes nothing and keeps the history.
    f.knob.commitText("4");
    const char junk[] = "junk";
    f.processor.setStateInformation(junk, sizeof(junk));
    CHECK(f.um().canUndo());
}

TEST_CASE("Undo: the preset modified flag follows undo and redo", "[undo][Preset]")
{
    KnobFixture f;
    auto& pm = f.processor.getPresetManager();
    REQUIRE(pm.loadInit().wasOk());
    CHECK_FALSE(pm.isModified());

    f.knob.commitText("4");
    CHECK(pm.isModified());
    CHECK(pm.getDisplayName().endsWith("*"));

    f.um().undo();
    CHECK_FALSE(pm.isModified());
    CHECK_FALSE(pm.getDisplayName().endsWith("*"));

    f.um().redo();
    CHECK(pm.isModified());

    // Two steps: modified until both are undone.
    f.knob.commitText("-4");
    f.um().undo();
    CHECK(pm.isModified());
    f.um().undo();
    CHECK_FALSE(pm.isModified());
}

TEST_CASE("Undo: the top bar buttons follow the history and drive it", "[undo][gui]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    auto& um = processor.getUndoManager();
    auto& top = editor.getTopBar();

    editor.refreshNow();
    CHECK_FALSE(top.undoButton().isEnabled());
    CHECK_FALSE(top.redoButton().isEnabled());
    CHECK(top.undoButton().getTooltip() == "Undo");
    CHECK(top.redoButton().getTooltip() == "Redo");

    editor.getStrip(0); // strips exist
    ui::LabelledKnob knob(processor.getAPVTS(), pid::inGain, "In", "hint",
                          ui::LabelledKnob::Fmt::dbSignedUnit, true, 44);
    knob.commitText("3");
    editor.refreshNow();
    CHECK(top.undoButton().isEnabled());
    CHECK_FALSE(top.redoButton().isEnabled());

    top.undoButton().onClick();
    CHECK(raw(processor, pid::inGain) == Approx(0.0f).margin(1.0e-3f));
    CHECK_FALSE(top.undoButton().isEnabled()); // updated right away
    CHECK(top.redoButton().isEnabled());

    top.redoButton().onClick();
    CHECK(raw(processor, pid::inGain) == Approx(3.0f).margin(0.02f));
    CHECK(top.undoButton().isEnabled());
    CHECK_FALSE(top.redoButton().isEnabled());

    processor.getPresetManager().loadInit();
    editor.refreshNow();
    CHECK_FALSE(top.undoButton().isEnabled());
    CHECK_FALSE(top.redoButton().isEnabled());
    CHECK_FALSE(um.canUndo());
}

TEST_CASE("Undo: the buttons fit at the base size, with 5 bands and in the compact layout", "[undo][gui]")
{
    PentagritAudioProcessor processor;
    automate(processor, pid::numBands, 5.0f);
    PentagritAudioProcessorEditor editor(processor);
    for (auto layout : {ui::Layout::full, ui::Layout::compact})
    {
        auto o = editor.getUiOptions();
        o.layout = layout;
        editor.setUiOptions(o);
        editor.refreshNow();
        auto& top = editor.getTopBar();
        const auto u = top.undoButton().getBounds(), r = top.redoButton().getBounds();
        const auto slot = top.presetSlot().getBounds();
        CHECK(top.getLocalBounds().contains(u));
        CHECK(top.getLocalBounds().contains(r));
        CHECK_FALSE(u.intersects(r));
        CHECK_FALSE(u.intersects(slot));
        CHECK_FALSE(r.intersects(slot));
        CHECK_FALSE(r.intersects(top.settingsButton().getBounds()));
        CHECK(slot.getWidth() >= 300);
    }
    // Both themes of the icons render something (light and dark styles).
    for (auto style : {ui::Style::studioDark, ui::Style::paperLight})
    {
        auto o = editor.getUiOptions();
        o.style = style;
        editor.setUiOptions(o);
        editor.getTopBar().setHistoryState(true, false);
        auto img = editor.getTopBar().undoButton().createComponentSnapshot(
            editor.getTopBar().undoButton().getLocalBounds(), false, 1.0f);
        CHECK(img.isValid());
    }
}

TEST_CASE("Undo: keyboard shortcuts", "[undo][gui]")
{
    PentagritAudioProcessor processor;
    PentagritAudioProcessorEditor editor(processor);
    ui::LabelledKnob knob(processor.getAPVTS(), pid::inGain, "In", "hint",
                          ui::LabelledKnob::Fmt::dbSignedUnit, true, 44);

    const auto ctrl = juce::ModifierKeys::ctrlModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    knob.commitText("3");

    CHECK_FALSE(editor.keyPressed(juce::KeyPress('z', juce::ModifierKeys(), 0))); // plain Z
    CHECK(raw(processor, pid::inGain) == Approx(3.0f).margin(0.02f));

    CHECK(editor.keyPressed(juce::KeyPress('z', juce::ModifierKeys(ctrl), 'z')));
    CHECK(raw(processor, pid::inGain) == Approx(0.0f).margin(1.0e-3f));

    CHECK(editor.keyPressed(juce::KeyPress('Z', juce::ModifierKeys(ctrl | shift), 'Z')));
    CHECK(raw(processor, pid::inGain) == Approx(3.0f).margin(0.02f));

    CHECK(editor.keyPressed(juce::KeyPress('z', juce::ModifierKeys(ctrl), 'z')));
    CHECK(editor.keyPressed(juce::KeyPress('y', juce::ModifierKeys(ctrl), 'y')));
    CHECK(raw(processor, pid::inGain) == Approx(3.0f).margin(0.02f));

    // Command (macOS) works like Ctrl.
    CHECK(
        editor.keyPressed(juce::KeyPress('z', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 'z')));
    CHECK(raw(processor, pid::inGain) == Approx(0.0f).margin(1.0e-3f));
    CHECK(editor.keyPressed(
        juce::KeyPress('z', juce::ModifierKeys(juce::ModifierKeys::commandModifier | shift), 'Z')));
    CHECK(raw(processor, pid::inGain) == Approx(3.0f).margin(0.02f));

    // Nothing to undo: still consumed, nothing changes.
    processor.clearUndoHistory();
    CHECK(editor.keyPressed(juce::KeyPress('z', juce::ModifierKeys(ctrl), 'z')));
    CHECK(raw(processor, pid::inGain) == Approx(3.0f).margin(0.02f));
    CHECK_FALSE(editor.keyPressed(juce::KeyPress('x', juce::ModifierKeys(ctrl), 'x')));
}
