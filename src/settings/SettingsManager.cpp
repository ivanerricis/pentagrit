// SPDX-License-Identifier: AGPL-3.0-or-later
#include "settings/SettingsManager.h"

#include <cmath>

namespace pentagrit
{
namespace
{
struct Overrides
{
    juce::File file;
    bool readOnly = false;
    bool ignoreLastPreset = false;
};

Overrides& overrides()
{
    static Overrides o;
    return o;
}

constexpr const char* kZoom = "uiZoom";
constexpr const char* kTheme = "uiTheme";
constexpr const char* kLayout = "uiLayout";
constexpr const char* kTooltips = "tooltips";
constexpr const char* kDblClick = "dblClick";
constexpr const char* kKnobDrag = "knobDrag";
constexpr const char* kKnobSens = "knobSens";
constexpr const char* kMeterHold = "meterHold";
constexpr const char* kMeterRelease = "meterRelease";
constexpr const char* kMeterRefresh = "meterRefresh";
constexpr const char* kShowCpu = "showCpu";
constexpr const char* kPresetFolder = "presetFolder";
constexpr const char* kStartupMode = "startupMode";
constexpr const char* kStartupName = "startupName";
constexpr const char* kStartupCategory = "startupCategory";
constexpr const char* kStartupFactory = "startupFactory";
constexpr const char* kLastName = "lastPresetName";
constexpr const char* kLastCategory = "lastPresetCategory";
constexpr const char* kLastFactory = "lastPresetFactory";
constexpr const char* kCheckUpdates = "checkUpdates";

int nearestOf(int v, std::initializer_list<int> allowed, int fallback)
{
    for (int a : allowed)
        if (a == v)
            return v;
    return fallback;
}
} // namespace

juce::File SettingsManager::getDefaultFile()
{
    if (overrides().file != juce::File())
        return overrides().file;
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("Pentagrit").getChildFile("settings.settings");
}

void SettingsManager::setTestOverrides(const juce::File& file, bool readOnly, bool ignoreLastPreset)
{
    overrides() = {file, readOnly, ignoreLastPreset};
}

void SettingsManager::setIgnoreLastPreset(bool ignore)
{
    overrides().ignoreLastPreset = ignore;
}

SettingsManager::SettingsManager()
{
    init(getDefaultFile(), overrides().readOnly);
}

SettingsManager::SettingsManager(const juce::File& file)
{
    init(file, false);
}

SettingsManager::~SettingsManager()
{
    if (props_ != nullptr)
        props_->saveIfNeeded();
}

void SettingsManager::init(const juce::File& file, bool readOnly)
{
    file_ = file;
    lock_ = std::make_unique<juce::InterProcessLock>(
        "PentagritSettings" + juce::String::toHexString(file.getFullPathName().hashCode64()));
    juce::PropertiesFile::Options o;
    o.applicationName = "Pentagrit";
    o.filenameSuffix = ".settings";
    o.folderName = "Pentagrit";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = -1; // saved explicitly (saveIfNeeded) after each change
    o.processLock = lock_.get();
    o.doNotSave = readOnly;
    props_ = std::make_unique<juce::PropertiesFile>(file, o);
    // A saved file always has properties; an existing non-empty file without any is unreadable (corrupt).
    loadedOk_ = !file.existsAsFile() || file.getSize() == 0 || props_->getAllProperties().size() > 0;
    readFromProperties();
    lastModified_ = file_.getLastModificationTime();
}

UserSettings SettingsManager::sanitised(UserSettings s)
{
    s.zoomPercent = nearestOf(s.zoomPercent, {75, 100, 125, 150, 200}, 100);
    s.sensitivity = juce::jlimit(0.0f, 1.0f, std::isfinite(s.sensitivity) ? s.sensitivity : 0.5f);
    s.meterHoldMs = juce::jlimit(0, UserSettings::kInfiniteHold, s.meterHoldMs);
    s.meterRelease = juce::jlimit(5.0f, 60.0f, std::isfinite(s.meterRelease) ? s.meterRelease : 20.0f);
    s.meterRefreshHz = nearestOf(s.meterRefreshHz, {30, 60}, 30);
    s.presetFolder = s.presetFolder.trim();
    if (!s.startup.isValid())
        s.startupMode = s.startupMode == StartupMode::specific ? StartupMode::last : s.startupMode;
    return s;
}

void SettingsManager::readFromProperties()
{
    UserSettings s;
    auto& p = *props_;
    s.zoomPercent = p.getIntValue(kZoom, s.zoomPercent);
    s.style = ui::styleFromId(p.getValue(kTheme, "studioDark"));
    s.layout = p.getValue(kLayout, "full") == "compact" ? ui::Layout::compact : ui::Layout::full;
    s.tooltips = p.getBoolValue(kTooltips, s.tooltips);
    s.doubleClick = p.getValue(kDblClick, "reset") == "type" ? ui::DoubleClick::type : ui::DoubleClick::reset;
    s.knobMode =
        p.getValue(kKnobDrag, "vertical") == "circular" ? ui::KnobMode::circular : ui::KnobMode::vertical;
    s.sensitivity = static_cast<float>(p.getDoubleValue(kKnobSens, s.sensitivity));
    s.meterHoldMs = p.getIntValue(kMeterHold, s.meterHoldMs);
    s.meterRelease = static_cast<float>(p.getDoubleValue(kMeterRelease, s.meterRelease));
    s.meterRefreshHz = p.getIntValue(kMeterRefresh, s.meterRefreshHz);
    s.showCpu = p.getBoolValue(kShowCpu, s.showCpu);
    s.presetFolder = p.getValue(kPresetFolder, {});
    const auto mode = p.getValue(kStartupMode, "last");
    s.startupMode = mode == "init"       ? StartupMode::init
                    : mode == "specific" ? StartupMode::specific
                                         : StartupMode::last;
    s.startup = {p.getValue(kStartupName, {}), p.getValue(kStartupCategory, {}),
                 p.getBoolValue(kStartupFactory, true)};
    s.lastPreset = {p.getValue(kLastName, {}), p.getValue(kLastCategory, {}),
                    p.getBoolValue(kLastFactory, true)};
    s.checkUpdates = p.getBoolValue(kCheckUpdates, false);
    data_ = sanitised(s);
}

void SettingsManager::writeToProperties()
{
    auto& p = *props_;
    const auto& s = data_;
    p.setValue(kZoom, s.zoomPercent);
    p.setValue(kTheme, ui::styleId(s.style));
    p.setValue(kLayout, s.layout == ui::Layout::compact ? "compact" : "full");
    p.setValue(kTooltips, s.tooltips);
    p.setValue(kDblClick, s.doubleClick == ui::DoubleClick::type ? "type" : "reset");
    p.setValue(kKnobDrag, s.knobMode == ui::KnobMode::circular ? "circular" : "vertical");
    p.setValue(kKnobSens, static_cast<double>(s.sensitivity));
    p.setValue(kMeterHold, s.meterHoldMs);
    p.setValue(kMeterRelease, static_cast<double>(s.meterRelease));
    p.setValue(kMeterRefresh, s.meterRefreshHz);
    p.setValue(kShowCpu, s.showCpu);
    p.setValue(kPresetFolder, s.presetFolder);
    p.setValue(kStartupMode, s.startupMode == StartupMode::init       ? "init"
                             : s.startupMode == StartupMode::specific ? "specific"
                                                                      : "last");
    p.setValue(kStartupName, s.startup.name);
    p.setValue(kStartupCategory, s.startup.category);
    p.setValue(kStartupFactory, s.startup.factory);
    p.setValue(kLastName, s.lastPreset.name);
    p.setValue(kLastCategory, s.lastPreset.category);
    p.setValue(kLastFactory, s.lastPreset.factory);
    p.setValue(kCheckUpdates, s.checkUpdates);
    p.saveIfNeeded();
    lastModified_ = file_.getLastModificationTime();
}

void SettingsManager::set(const UserSettings& s)
{
    data_ = sanitised(s);
    writeToProperties();
    sendSynchronousChangeMessage();
}

void SettingsManager::resetToDefaults()
{
    UserSettings s;
    s.lastPreset = data_.lastPreset;
    data_ = s;
    props_->clear();
    writeToProperties();
    sendSynchronousChangeMessage();
}

void SettingsManager::setLastPreset(const PresetRef& ref)
{
    if (ref == data_.lastPreset || overrides().ignoreLastPreset)
        return;
    data_.lastPreset = ref;
    writeToProperties();
}

bool SettingsManager::reloadIfChanged()
{
    const auto mod = file_.getLastModificationTime();
    if (mod == lastModified_)
        return false;
    lastModified_ = mod;
    const auto before = data_;
    props_->reload();
    readFromProperties();
    const bool differs = before.zoomPercent != data_.zoomPercent || before.style != data_.style ||
                         before.layout != data_.layout || before.tooltips != data_.tooltips ||
                         before.doubleClick != data_.doubleClick || before.knobMode != data_.knobMode ||
                         before.sensitivity != data_.sensitivity || before.meterHoldMs != data_.meterHoldMs ||
                         before.meterRelease != data_.meterRelease ||
                         before.meterRefreshHz != data_.meterRefreshHz || before.showCpu != data_.showCpu ||
                         before.presetFolder != data_.presetFolder ||
                         before.startupMode != data_.startupMode || !(before.startup == data_.startup) ||
                         before.checkUpdates != data_.checkUpdates;
    if (differs)
        sendSynchronousChangeMessage();
    return differs;
}

ui::UiOptions SettingsManager::toUiOptions(const UserSettings& s)
{
    ui::UiOptions o;
    o.style = s.style;
    o.layout = s.layout;
    o.tooltips = s.tooltips;
    o.dc = s.doubleClick;
    o.knob = s.knobMode;
    o.sensitivity = s.sensitivity;
    o.meterHoldSec =
        s.meterHoldMs >= UserSettings::kInfiniteHold ? -1.0f : static_cast<float>(s.meterHoldMs) / 1000.0f;
    o.meterReleaseDbPerSec = s.meterRelease;
    o.refreshHz = s.meterRefreshHz;
    o.showCpu = s.showCpu;
    o.zoomPercent = s.zoomPercent;
    return o;
}

juce::File SettingsManager::getEffectivePresetFolder(const UserSettings& s)
{
    return s.presetFolder.isEmpty() ? PresetManager::getDefaultUserFolder() : juce::File(s.presetFolder);
}

juce::File SettingsManager::getEffectivePresetFolder() const
{
    return getEffectivePresetFolder(data_);
}

bool SettingsManager::applyStartupPreset(const UserSettings& s, PresetManager& presets)
{
    if (s.startupMode == StartupMode::init)
        return presets.loadInit().wasOk();

    const auto& ref = s.startupMode == StartupMode::specific ? s.startup : s.lastPreset;
    if (!ref.isValid())
        return false;
    const PresetEntry wanted{ref.name, ref.category, {}, ref.factory, {}};
    const auto& list = ref.factory ? presets.getFactoryPresets() : presets.getUserPresets();
    for (const auto& e : list)
        if (e.isSameAs(wanted))
            return presets.loadPreset(e).wasOk();
    return false;
}
} // namespace pentagrit
