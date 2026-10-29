// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "ui/BandStrip.h"
#include "ui/CrossoverDisplay.h"
#include "ui/Footer.h"
#include "ui/GlobalPanel.h"
#include "ui/PentagritLookAndFeel.h"
#include "ui/TopBar.h"
#include "ui/UiOptions.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace pentagrit
{
// Custom editor (WP5b). All content lives in `content()`, laid out at the base size (1100 x 680 full, 1100 x
// 540 compact) and scaled uniformly to the editor size, so the editor can be resized with a fixed aspect
// ratio. Zoom is applied with setScaleFactor() (see setUiOptions).
class PentagritAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PentagritAudioProcessorEditor(PentagritAudioProcessor& processor);
    ~PentagritAudioProcessorEditor() override;

    // Applies every option live. Safe to call repeatedly (WP9 feeds this from the user settings).
    void setUiOptions(const ui::UiOptions& options);
    const ui::UiOptions& getUiOptions() const noexcept { return options_; }

    // Hooks for the other work packages.
    // WP8: add the PresetBar as a child of presetSlot(); it is stretched over the slot.
    juce::Component& presetSlot() noexcept { return topBar_.presetSlot(); }
    // WP9: assign to open the settings panel (the gear button in the top bar).
    std::function<void()>& onSettingsClicked() noexcept { return topBar_.onSettingsClicked; }
    // Parent for overlays (settings panel): same coordinate system as the layout (base size, scaled with the
    // editor).
    juce::Component& content() noexcept { return content_; }
    ui::TopBar& getTopBar() noexcept { return topBar_; }
    ui::Footer& getFooter() noexcept { return footer_; }
    // Shows `text` in the footer for `ms` milliseconds (preset toasts).
    void showToast(const juce::String& text, int ms = 2600);

    // Inspection (tests).
    int numBands() const noexcept { return numBands_; }
    bool isCompact() const noexcept { return options_.layout == ui::Layout::compact; }
    ui::BandStrip& getStrip(int band) noexcept { return *strips_[static_cast<size_t>(band)]; }
    ui::CrossoverDisplay& getCrossover() noexcept { return crossover_; }
    // Runs one UI refresh synchronously (what the timer does).
    void refreshNow();

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    // Holds the whole UI at the base size; scaled by a transform.
    class Content final : public juce::Component
    {
    public:
        std::function<void()> onResized;
        void resized() override
        {
            if (onResized)
                onResized();
        }
        void paint(juce::Graphics& g) override { g.fillAll(ui::pal(*this).bg0); }
    };

    void timerCallback() override;
    void layoutContent();
    void applyNumBands(int n, bool fromUser);
    void applyThemeToChildren();
    int baseHeight() const noexcept { return isCompact() ? ui::kCompactHeight : ui::kFullHeight; }
    void updateConstraints();

    PentagritAudioProcessor& processor_;
    ui::PentagritLookAndFeel lnf_;
    ui::UiOptions options_;
    int numBands_ = 3;
    double lastTickMs_ = 0.0;
    double toastUntilMs_ = 0.0;

    Content content_;
    ui::TopBar topBar_;
    ui::CrossoverDisplay crossover_;
    std::array<std::unique_ptr<ui::BandStrip>, kMaxBands> strips_;
    ui::GlobalPanel global_;
    ui::Footer footer_;
    std::unique_ptr<juce::ParameterAttachment> numBandsAttachment_;
    std::unique_ptr<juce::TooltipWindow> tooltip_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PentagritAudioProcessorEditor)
};
} // namespace pentagrit
