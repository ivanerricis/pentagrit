// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <vector>

namespace pentagrit
{
// One entry of the preset library (factory or user).
struct PresetEntry
{
    juce::String name;
    juce::String category;
    juce::String author;
    bool factory = false;
    juce::File file; // user presets only

    bool isSameAs(const PresetEntry& o) const noexcept
    {
        return factory == o.factory && name == o.name && category == o.category;
    }
};

using PresetParamValues = std::map<juce::String, float>;

// Preset manager. MESSAGE THREAD ONLY; never touched by the audio thread.
//
// Presets store the parameter values of the APVTS, except the project-level settings that must survive a
// preset change: osPhase, osRender (audio quality) and the global bypass. The current preset (name, category,
// factory flag) is kept in the APVTS state, so it is saved with the DAW project.
//
// Preset file format (.pgp, XML):
//   <PENTAGRIT_PRESET stateVersion="1" name=".." category=".." author="..">
//     <PENTAGRIT_STATE><PARAM id="inGain" value="-3"/> ...</PENTAGRIT_STATE>
//   </PENTAGRIT_PRESET>
// Parameters missing from a preset take their default value; out of range values are clamped.
class PresetManager final : public juce::ChangeBroadcaster,
                            private juce::ValueTree::Listener,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::AsyncUpdater
{
public:
    using ParamValues = PresetParamValues;
    enum class Slot
    {
        A,
        B
    };

    static constexpr const char* kFileExtension = ".pgp";
    static constexpr const char* kUnsortedCategory = "Unsorted";

    // userFolder: root of the user presets; default is the platform folder (getDefaultUserFolder()).
    explicit PresetManager(juce::AudioProcessorValueTreeState& apvts, const juce::File& userFolder = {});
    ~PresetManager() override;

    // ---- Folders ----
    static juce::File getDefaultUserFolder();
    juce::File getUserFolder() const { return userFolder_; }
    void setUserFolder(const juce::File& folder); // re-scans; does not touch the current state

    // ---- Library ----
    void rescan(); // re-reads the user folder (corrupt files are skipped, see getSkippedFiles())
    const std::vector<PresetEntry>& getFactoryPresets() const { return factoryEntries_; }
    const std::vector<PresetEntry>& getUserPresets() const { return userEntries_; }
    juce::StringArray getFactoryCategories() const;
    juce::StringArray getUserCategories() const; // sub-folders of the user folder, including empty ones
    std::vector<PresetEntry> getPresetsInCategory(bool factory, const juce::String& category) const;
    std::vector<PresetEntry>
    getOrderedPresets() const; // menu order: Factory categories, then User categories
    juce::StringArray getSkippedFiles() const { return skippedFiles_; }

    // ---- Current preset ----
    juce::String getCurrentName();
    juce::String getCurrentCategory();
    bool isCurrentFactory();
    bool isModified();             // true when parameters differ from the loaded preset
    juce::String getDisplayName(); // "Name" or "Name *" when modified
    PresetEntry getCurrentEntry();
    void setAuthor(const juce::String& author) { author_ = author; }

    // ---- Load ----
    juce::Result loadPreset(const PresetEntry& entry);
    juce::Result loadInit();
    juce::Result loadNext();     // wraps around
    juce::Result loadPrevious(); // wraps around

    // ---- Operations on user presets ----
    juce::Result save(); // overwrite the current user preset
    juce::Result saveAs(const juce::String& name, const juce::String& category, bool overwrite = false);
    bool userPresetExists(const juce::String& name, const juce::String& category) const;
    juce::Result rename(const juce::String& newName); // current user preset
    juce::Result deleteCurrent();                     // current user preset, then loads Init
    juce::Result duplicateCurrent(); // saves the current state as "<name> copy" and selects it
    juce::Result importPreset(const juce::File& file, PresetEntry* created = nullptr);
    juce::Result exportCurrent(const juce::File& destination);

    // ---- A/B compare ----
    Slot getActiveSlot() const { return activeSlot_; }
    void selectSlot(Slot slot); // stores the current state in the active slot, then recalls the other one

    // ---- Utilities (public for the UI and the tests) ----
    // Returns an empty string when the name is not usable as a file name.
    static juce::String sanitiseName(const juce::String& name);
    static bool isPresetParameter(const juce::String& paramID);
    // Parses a preset document (validates tag and stateVersion).
    static juce::Result parsePreset(const juce::XmlElement& root, PresetEntry& meta, ParamValues& values);
    static juce::Result parsePresetFile(const juce::File& file, PresetEntry& meta, ParamValues& values);

private:
    struct SlotData
    {
        bool valid = false;
        ParamValues values;
        PresetEntry ref;
        bool hasBaseline = false;
        ParamValues baseline;
    };

    // juce::ValueTree::Listener
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected(juce::ValueTree&) override;
    // juce::AudioProcessorValueTreeState::Listener (may be called from any thread)
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    // juce::AsyncUpdater
    void handleAsyncUpdate() override;

    void ensureSynced();
    ParamValues captureValues() const;
    ParamValues resolveValues(const ParamValues& partial) const; // fills defaults, clamps
    void applyValues(const ParamValues& values, const PresetEntry* ref);
    bool computeModified() const;
    void updateModifiedFlag();
    void setCurrent(const PresetEntry& entry, const ParamValues& baseline);
    juce::Result step(int direction);
    juce::File fileFor(const juce::String& category, const juce::String& name) const;
    juce::String uniqueName(const juce::String& base, const juce::String& category) const;
    juce::Result writePresetFile(const juce::File& dest, const PresetEntry& meta,
                                 const ParamValues& values) const;
    static juce::Result resolveCategory(const juce::String& category, juce::String& out);
    void notify();

    juce::AudioProcessorValueTreeState& apvts_;
    juce::File userFolder_;
    juce::String author_;

    std::vector<PresetEntry> factoryEntries_;
    std::vector<ParamValues> factoryValues_;
    std::vector<PresetEntry> userEntries_;
    juce::StringArray skippedFiles_;

    PresetEntry current_;
    bool hasBaseline_ = false;
    ParamValues baseline_;
    bool needsSync_ = false;
    bool lastModified_ = false;
    int suppress_ = 0;

    Slot activeSlot_ = Slot::A;
    std::array<SlotData, 2> slots_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};
} // namespace pentagrit
