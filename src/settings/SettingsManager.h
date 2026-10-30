// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "presets/PresetManager.h"
#include "ui/UiOptions.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace pentagrit
{
enum class StartupMode
{
    last,    // the preset used last (in any instance)
    init,    // the Init preset
    specific // `startup`
};

// Identifies a preset (name + category + factory flag), independent of its file.
struct PresetRef
{
    juce::String name;
    juce::String category;
    bool factory = true;

    bool isValid() const noexcept { return name.isNotEmpty(); }
    bool operator==(const PresetRef& o) const noexcept
    {
        return name == o.name && category == o.category && factory == o.factory;
    }
};

// Per-user settings (never part of the project state or of presets).
struct UserSettings
{
    int zoomPercent = 100; // 75 / 100 / 125 / 150 / 200
    ui::Style style = ui::Style::studioDark;
    ui::Layout layout = ui::Layout::full;
    bool tooltips = true;
    ui::KnobMode knobMode = ui::KnobMode::vertical;
    float valueStepDb = 0.0f;   // dB knobs drag step: 0 (continuous) / 0.1 / 0.5 / 1
    float sensitivity = 0.5f;   // 0 (fine) .. 1 (fast)
    int meterHoldMs = 1000;     // 0 = off, 99999 = infinite
    float meterRelease = 20.0f; // dB/s, 5..60
    int meterRefreshHz = 30;    // 30 / 60
    bool showCpu = true;
    juce::String presetFolder; // empty = platform default (PresetManager::getDefaultUserFolder())
    StartupMode startupMode = StartupMode::last;
    PresetRef startup; // used when startupMode == specific
    PresetRef lastPreset;
    bool checkUpdates = false; // opt-in, off by default

    static constexpr int kInfiniteHold = 99999;
};

// Per-user settings stored in a juce::PropertiesFile (<app data>/Pentagrit/settings.settings). One instance
// is shared by every plugin instance of the process (juce::SharedResourcePointer<SettingsManager>); the file
// is process-safe (inter-process lock) and a missing or corrupt file simply yields the defaults. MESSAGE
// THREAD ONLY. Broadcasts a (synchronous) change message when the settings change.
class SettingsManager final : public juce::ChangeBroadcaster
{
public:
    // The update check needs the public GitHub Releases API: unavailable while the repository is private.
    static constexpr bool kUpdateCheckAvailable = false;

    SettingsManager(); // default file
    explicit SettingsManager(const juce::File& file);
    ~SettingsManager() override;

    static juce::File getDefaultFile();
    // Tests only: redirects the default file, optionally never writing it (`readOnly`) and optionally
    // ignoring setLastPreset() (so that presets loaded by one test do not leak into the next processor).
    // Takes effect for SettingsManager instances created afterwards.
    static void setTestOverrides(const juce::File& file, bool readOnly, bool ignoreLastPreset);
    static void setIgnoreLastPreset(bool ignore);
    juce::File getFile() const { return file_; }
    bool loadedOk() const noexcept { return loadedOk_; }

    const UserSettings& get() const noexcept { return data_; }
    // Validates (clamps unknown values), stores, saves and notifies.
    void set(const UserSettings& s);
    // Resets everything except the last used preset; saves and notifies.
    void resetToDefaults();
    // Records the last used preset (saved, no notification: it does not affect any UI).
    void setLastPreset(const PresetRef& ref);
    // Re-reads the file when another process changed it. Returns true when the settings changed.
    bool reloadIfChanged();

    ui::UiOptions toUiOptions() const { return toUiOptions(data_); }
    static ui::UiOptions toUiOptions(const UserSettings& s);
    // The configured folder, or the platform default.
    juce::File getEffectivePresetFolder() const;
    static juce::File getEffectivePresetFolder(const UserSettings& s);

    // Applies the startup preset policy. Returns true when a preset was loaded. Does nothing (false) when the
    // preset cannot be found (e.g. "last" without history, or a deleted user preset).
    static bool applyStartupPreset(const UserSettings& s, PresetManager& presets);
    static PresetRef toRef(const PresetEntry& e) { return {e.name, e.category, e.factory}; }

private:
    void init(const juce::File& file, bool readOnly);
    void readFromProperties();
    void writeToProperties();
    static UserSettings sanitised(UserSettings s);

    juce::File file_;
    std::unique_ptr<juce::InterProcessLock> lock_;
    std::unique_ptr<juce::PropertiesFile> props_;
    UserSettings data_;
    juce::Time lastModified_;
    bool loadedOk_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsManager)
};
} // namespace pentagrit
