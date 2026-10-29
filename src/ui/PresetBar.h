// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "presets/PresetManager.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pentagrit
{
// Preset bar: [<] [name *] [>] [...]  [A] [B]. Self-contained and neutral: colours come from the active
// LookAndFeel (TextButton / PopupMenu / AlertWindow colour IDs), so the editor theme applies automatically.
class PresetBar final : public juce::Component, private juce::ChangeListener
{
public:
    explicit PresetBar(PresetManager& manager);
    ~PresetBar() override;

    void resized() override;
    int getPreferredWidth() const;

    // Optional sink for short status/error messages (e.g. the editor footer toast). When unset, errors are
    // shown in a message box.
    std::function<void(const juce::String&)> onMessage;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh();
    void showBrowserMenu();
    void showActionsMenu();
    void promptSaveAs();
    void promptRename();
    void confirmDelete();
    void chooseImport();
    void chooseExport();
    void report(const juce::Result& result, const juce::String& successMessage = {});

    PresetManager& manager_;
    juce::TextButton prev_{juce::CharPointer_UTF8("\xe2\x80\xb9")};
    juce::TextButton name_;
    juce::TextButton next_{juce::CharPointer_UTF8("\xe2\x80\xba")};
    juce::TextButton menu_{juce::CharPointer_UTF8("\xe2\x80\xa6")};
    juce::TextButton slotA_{"A"};
    juce::TextButton slotB_{"B"};
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};
} // namespace pentagrit
