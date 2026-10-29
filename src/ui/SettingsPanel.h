// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "settings/SettingsManager.h"
#include "ui/Widgets.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pentagrit::ui
{
// Modal settings overlay (gear button), 4 tabs: Interface, Audio, Presets, About & Support. It fills its
// parent (the editor content); the card is centred. Esc, the Done button and a click outside the card call
// `onClose`. Interface/Presets/About changes go to the shared SettingsManager (per user); Audio is bound to
// the APVTS parameters osPhase/osRender (saved with the project).
class SettingsPanel final : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    enum Tab
    {
        tabInterface = 0,
        tabAudio,
        tabPresets,
        tabAbout,
        numTabs
    };

    explicit SettingsPanel(PentagritAudioProcessor& processor);
    ~SettingsPanel() override;

    std::function<void()> onClose;

    void showTab(int tab);
    int currentTab() const noexcept { return tab_; }
    // Text of the footer message line (tests).
    juce::String message() const { return message_; }

    // Debug information copied by "Copy debug info" (version, OS, host, sample rate, block size, latency,
    // OS).
    static juce::String buildDebugInfo(PentagritAudioProcessor& processor);

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void lookAndFeelChanged() override;

private:
    class Page;
    class Row;
    class Segmented;
    class Switch;

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuildPage();
    void layoutPage();
    void say(const juce::String& text);
    void apply(const std::function<void(UserSettings&)>& edit);
    void updateLatencyText();

    void buildInterface(Page&);
    void buildAudio(Page&);
    void buildPresets(Page&);
    void buildAbout(Page&);

    void chooseFolder();
    void openFolder();
    void showStartupMenu(juce::Component& target);
    juce::String startupLabel() const;
    void confirmReset();

    PentagritAudioProcessor& processor_;
    SettingsManager& settings_;
    int tab_ = tabInterface;
    int guard_ = 0;
    juce::String message_;

    std::unique_ptr<Segmented> tabs_;
    juce::TextButton done_{"Done"};
    juce::Viewport viewport_;
    juce::Rectangle<int> card_, headerArea_, messageArea_;
    juce::Label* latencyLabel_ = nullptr;
    juce::TextEditor* folderEditor_ = nullptr;
    juce::TextButton* startupButton_ = nullptr;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};
} // namespace pentagrit::ui
