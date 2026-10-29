// SPDX-License-Identifier: AGPL-3.0-or-later
#include "presets/PresetManager.h"

#include "params/ParamIDs.h"
#include "presets/FactoryPresets.h"
#include <algorithm>
#include <cmath>

namespace pentagrit
{
namespace
{
const juce::Identifier kPropName{"presetName"};
const juce::Identifier kPropCategory{"presetCategory"};
const juce::Identifier kPropFactory{"presetFactory"};
const juce::Identifier kParamType{"PARAM"};
const juce::Identifier kIdProp{"id"};
const juce::Identifier kValueProp{"value"};
constexpr const char* kPresetTag = "PENTAGRIT_PRESET";
constexpr int kMaxNameLength = 64;

bool isReservedWindowsName(const juce::String& s)
{
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL"};
    const auto up = s.upToFirstOccurrenceOf(".", false, false).trim().toUpperCase();
    for (auto* r : reserved)
        if (up == r)
            return true;
    if (up.length() == 4 && (up.startsWith("COM") || up.startsWith("LPT")) && up[3] >= '1' && up[3] <= '9')
        return true;
    return false;
}

bool samePath(const juce::File& a, const juce::File& b)
{
    return juce::File::areFileNamesCaseSensitive()
               ? a == b
               : a.getFullPathName().equalsIgnoreCase(b.getFullPathName());
}

int compareNoCase(const juce::String& a, const juce::String& b)
{
    return a.compareNatural(b, false);
}

float paramDefault(const juce::RangedAudioParameter& p)
{
    return p.convertFrom0to1(p.getDefaultValue());
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Static helpers

juce::File PresetManager::getDefaultUserFolder()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("Pentagrit").getChildFile("Presets");
}

juce::String PresetManager::sanitiseName(const juce::String& name)
{
    juce::String out;
    bool lastSpace = false;
    for (auto c : name)
    {
        if (c < 32 || juce::String("\\/:*?\"<>|").containsChar(c))
            continue;
        if (juce::CharacterFunctions::isWhitespace(c))
        {
            if (!lastSpace && out.isNotEmpty())
                out << ' ';
            lastSpace = true;
        }
        else
        {
            out << juce::String::charToString(c);
            lastSpace = false;
        }
    }
    out = out.substring(0, kMaxNameLength).trim();
    while (out.endsWithChar('.') || out.endsWithChar(' '))
        out = out.dropLastCharacters(1);
    if (out.isEmpty() || isReservedWindowsName(out))
        return {};
    return out;
}

bool PresetManager::isPresetParameter(const juce::String& id)
{
    return id != pid::osPhase && id != pid::osRender && id != pid::bypass;
}

juce::Result PresetManager::parsePreset(const juce::XmlElement& root, PresetEntry& meta, ParamValues& values)
{
    if (!root.hasTagName(kPresetTag))
        return juce::Result::fail("Not a Pentagrit preset");

    if (root.hasAttribute("stateVersion"))
    {
        const auto text = root.getStringAttribute("stateVersion").trim();
        if (!text.containsOnly("0123456789") || text.isEmpty())
            return juce::Result::fail("Invalid preset version");
        const int version = text.getIntValue();
        if (version < 1)
            return juce::Result::fail("Invalid preset version");
        if (version > kStateVersion)
            return juce::Result::fail("Preset was created by a newer version of Pentagrit");
        // Migration of older versions goes here (only version 1 exists for now).
    }

    const auto* state = root.getChildByName(kStateTag);
    if (state == nullptr)
        return juce::Result::fail("Preset has no state");

    PresetEntry m;
    m.name = root.getStringAttribute("name");
    m.category = root.getStringAttribute("category");
    m.author = root.getStringAttribute("author");

    ParamValues v;
    for (const auto* p : state->getChildWithTagNameIterator("PARAM"))
    {
        const auto id = p->getStringAttribute("id");
        if (id.isEmpty() || !p->hasAttribute("value") || !isPresetParameter(id))
            continue;
        const double value = p->getDoubleAttribute("value", std::nan(""));
        if (!std::isfinite(value))
            continue;
        v[id] = static_cast<float>(value);
    }

    meta = m;
    values = std::move(v);
    return juce::Result::ok();
}

juce::Result PresetManager::parsePresetFile(const juce::File& file, PresetEntry& meta, ParamValues& values)
{
    if (!file.existsAsFile())
        return juce::Result::fail("File not found");
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return juce::Result::fail("File is not a valid preset (unreadable XML)");
    auto r = parsePreset(*xml, meta, values);
    if (r.failed())
        return r;
    meta.file = file;
    return r;
}

// ---------------------------------------------------------------------------------------------------------------

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& apvts, const juce::File& userFolder)
    : apvts_(apvts), userFolder_(userFolder == juce::File() ? getDefaultUserFolder() : userFolder)
{
    for (auto& f : loadFactoryPresets())
    {
        factoryEntries_.push_back(f.entry);
        factoryValues_.push_back(f.values);
    }
    rescan();

    apvts_.state.addListener(this);
    for (auto* p : apvts_.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p);
            r != nullptr && isPresetParameter(r->paramID))
            apvts_.addParameterListener(r->paramID, this);
    needsSync_ = true;
    ensureSynced();
}

PresetManager::~PresetManager()
{
    cancelPendingUpdate();
    for (auto* p : apvts_.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p);
            r != nullptr && isPresetParameter(r->paramID))
            apvts_.removeParameterListener(r->paramID, this);
    apvts_.state.removeListener(this);
}

void PresetManager::notify()
{
    sendSynchronousChangeMessage();
}

// ---- Library
// ---------------------------------------------------------------------------------------------------

void PresetManager::setUserFolder(const juce::File& folder)
{
    userFolder_ = folder == juce::File() ? getDefaultUserFolder() : folder;
    rescan();
    needsSync_ = true; // the current preset might now be found (or lost) in the new folder
    notify();
}

void PresetManager::rescan()
{
    userEntries_.clear();
    skippedFiles_.clear();

    auto scanDir = [this](const juce::File& dir, const juce::String& category)
    {
        for (const auto& f :
             dir.findChildFiles(juce::File::findFiles, false, juce::String("*") + kFileExtension))
        {
            PresetEntry meta;
            ParamValues values;
            if (parsePresetFile(f, meta, values).failed())
            {
                skippedFiles_.add(f.getFullPathName());
                continue;
            }
            meta.factory = false;
            meta.category = category; // the folder is authoritative
            if (meta.name.isEmpty())
                meta.name = f.getFileNameWithoutExtension();
            userEntries_.push_back(meta);
        }
    };

    if (userFolder_.isDirectory())
    {
        scanDir(userFolder_, kUnsortedCategory);
        for (const auto& d : userFolder_.findChildFiles(juce::File::findDirectories, false))
            scanDir(d, d.getFileName());
    }

    std::sort(userEntries_.begin(), userEntries_.end(),
              [](const PresetEntry& a, const PresetEntry& b)
              {
                  if (const int c = compareNoCase(a.category, b.category))
                      return c < 0;
                  return compareNoCase(a.name, b.name) < 0;
              });
}

juce::StringArray PresetManager::getFactoryCategories() const
{
    juce::StringArray cats;
    for (const auto& e : factoryEntries_)
        cats.addIfNotAlreadyThere(e.category);
    return cats;
}

juce::StringArray PresetManager::getUserCategories() const
{
    juce::StringArray cats;
    for (const auto& e : userEntries_)
        cats.addIfNotAlreadyThere(e.category);
    if (userFolder_.isDirectory())
        for (const auto& d : userFolder_.findChildFiles(juce::File::findDirectories, false))
            cats.addIfNotAlreadyThere(d.getFileName());
    cats.sort(true);
    return cats;
}

std::vector<PresetEntry> PresetManager::getPresetsInCategory(bool factory, const juce::String& category) const
{
    std::vector<PresetEntry> out;
    for (const auto& e : factory ? factoryEntries_ : userEntries_)
        if (e.category == category)
            out.push_back(e);
    return out;
}

std::vector<PresetEntry> PresetManager::getOrderedPresets() const
{
    std::vector<PresetEntry> out = factoryEntries_;
    out.insert(out.end(), userEntries_.begin(), userEntries_.end());
    return out;
}

// ---- Current preset
// --------------------------------------------------------------------------------------------

void PresetManager::ensureSynced()
{
    if (!needsSync_)
        return;
    needsSync_ = false;

    const auto& state = apvts_.state;
    ++suppress_;

    if (!state.hasProperty(kPropName))
    {
        // Fresh instance: the default parameter values are the Init preset.
        current_ = factoryEntries_.empty() ? PresetEntry{"Init", "Init", "Pentagrit", true, {}}
                                           : factoryEntries_.front();
        hasBaseline_ = true;
        baseline_ = captureValues();
        apvts_.state.setProperty(kPropName, current_.name, nullptr);
        apvts_.state.setProperty(kPropCategory, current_.category, nullptr);
        apvts_.state.setProperty(kPropFactory, true, nullptr);
        --suppress_;
        lastModified_ = false;
        return;
    }

    PresetEntry ref;
    ref.name = state.getProperty(kPropName).toString();
    ref.category = state.getProperty(kPropCategory).toString();
    ref.factory = static_cast<bool>(state.getProperty(kPropFactory, false));

    hasBaseline_ = false;
    baseline_.clear();
    const auto& list = ref.factory ? factoryEntries_ : userEntries_;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (!list[i].isSameAs(ref))
            continue;
        ref = list[i];
        if (ref.factory)
        {
            baseline_ = resolveValues(factoryValues_[i]);
            hasBaseline_ = true;
        }
        else
        {
            PresetEntry meta;
            ParamValues values;
            if (parsePresetFile(ref.file, meta, values).wasOk())
            {
                baseline_ = resolveValues(values);
                hasBaseline_ = true;
            }
        }
        break;
    }
    current_ = ref;
    --suppress_;
    lastModified_ = computeModified();
}

juce::String PresetManager::getCurrentName()
{
    ensureSynced();
    return current_.name;
}

juce::String PresetManager::getCurrentCategory()
{
    ensureSynced();
    return current_.category;
}

bool PresetManager::isCurrentFactory()
{
    ensureSynced();
    return current_.factory;
}

PresetEntry PresetManager::getCurrentEntry()
{
    ensureSynced();
    return current_;
}

bool PresetManager::isModified()
{
    ensureSynced();
    return computeModified();
}

juce::String PresetManager::getDisplayName()
{
    return getCurrentName() + (isModified() ? " *" : "");
}

PresetManager::ParamValues PresetManager::captureValues() const
{
    ParamValues out;
    for (auto* p : apvts_.processor.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (r == nullptr || !isPresetParameter(r->paramID))
            continue;
        if (auto* raw = apvts_.getRawParameterValue(r->paramID))
            out[r->paramID] = raw->load();
    }
    return out;
}

PresetManager::ParamValues PresetManager::resolveValues(const ParamValues& partial) const
{
    ParamValues out;
    for (auto* p : apvts_.processor.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (r == nullptr || !isPresetParameter(r->paramID))
            continue;
        float v = paramDefault(*r);
        if (auto it = partial.find(r->paramID); it != partial.end() && std::isfinite(it->second))
        {
            const auto range = r->getNormalisableRange();
            v = range.snapToLegalValue(range.getRange().clipValue(it->second));
        }
        out[r->paramID] = v;
    }
    return out;
}

void PresetManager::applyValues(const ParamValues& partial, const PresetEntry* ref)
{
    const auto values = resolveValues(partial);
    ++suppress_;

    auto tree = apvts_.copyState(); // keeps osPhase/osRender/bypass, UI size and any other property
    for (const auto& [id, value] : values)
    {
        auto child = tree.getChildWithProperty(kIdProp, id);
        if (!child.isValid())
        {
            child = juce::ValueTree(kParamType);
            child.setProperty(kIdProp, id, nullptr);
            tree.appendChild(child, nullptr);
        }
        child.setProperty(kValueProp, value, nullptr);
    }
    if (ref != nullptr)
    {
        tree.setProperty(kPropName, ref->name, nullptr);
        tree.setProperty(kPropCategory, ref->category, nullptr);
        tree.setProperty(kPropFactory, ref->factory, nullptr);
    }
    apvts_.replaceState(tree);

    --suppress_;
}

void PresetManager::setCurrent(const PresetEntry& entry, const ParamValues& baseline)
{
    current_ = entry;
    baseline_ = baseline;
    hasBaseline_ = true;
    needsSync_ = false;
    ++suppress_;
    apvts_.state.setProperty(kPropName, entry.name, nullptr);
    apvts_.state.setProperty(kPropCategory, entry.category, nullptr);
    apvts_.state.setProperty(kPropFactory, entry.factory, nullptr);
    --suppress_;
    lastModified_ = computeModified();
}

bool PresetManager::computeModified() const
{
    if (!hasBaseline_)
        return false;
    for (const auto& [id, value] : baseline_)
    {
        auto* raw = apvts_.getRawParameterValue(id);
        if (raw == nullptr)
            continue;
        const float now = raw->load();
        if (std::abs(now - value) > 1e-3f + 1e-5f * std::abs(value))
            return true;
    }
    return false;
}

void PresetManager::updateModifiedFlag()
{
    if (suppress_ > 0 || needsSync_)
        return;
    const bool m = computeModified();
    if (m != lastModified_)
    {
        lastModified_ = m;
        notify();
    }
}

void PresetManager::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    if (suppress_ > 0)
        return;

    if (tree.hasType(kParamType))
    {
        if (property == kValueProp && !needsSync_)
            updateModifiedFlag();
    }
    else if (tree == apvts_.state &&
             (property == kPropName || property == kPropCategory || property == kPropFactory))
    {
        // The state was replaced from outside (the DAW restored a project): re-read lazily.
        needsSync_ = true;
        sendChangeMessage();
    }
}

void PresetManager::valueTreeRedirected(juce::ValueTree&)
{
    // replaceState() (e.g. the DAW restoring a project) swaps the whole tree.
    if (suppress_ == 0)
    {
        needsSync_ = true;
        sendChangeMessage();
    }
}

void PresetManager::parameterChanged(const juce::String&, float)
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        updateModifiedFlag();
    else
        triggerAsyncUpdate();
}

void PresetManager::handleAsyncUpdate()
{
    updateModifiedFlag();
}

void PresetManager::valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&)
{
    if (suppress_ == 0)
        sendChangeMessage();
}

void PresetManager::valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int)
{
    if (suppress_ == 0)
        sendChangeMessage();
}

// ---- Load
// ------------------------------------------------------------------------------------------------------

juce::Result PresetManager::loadPreset(const PresetEntry& entry)
{
    ensureSynced();
    ParamValues values;
    PresetEntry resolved = entry;

    if (entry.factory)
    {
        bool found = false;
        for (size_t i = 0; i < factoryEntries_.size(); ++i)
            if (factoryEntries_[i].isSameAs(entry))
            {
                values = factoryValues_[i];
                resolved = factoryEntries_[i];
                found = true;
                break;
            }
        if (!found)
            return juce::Result::fail("Factory preset not found");
    }
    else
    {
        PresetEntry meta;
        auto file = entry.file;
        if (file == juce::File())
            file = fileFor(entry.category, entry.name);
        if (auto r = parsePresetFile(file, meta, values); r.failed())
            return r;
        resolved.file = file;
        resolved.author = meta.author;
        resolved.factory = false;
    }

    applyValues(values, &resolved);
    setCurrent(resolved, captureValues());
    notify();
    return juce::Result::ok();
}

juce::Result PresetManager::loadInit()
{
    for (const auto& e : factoryEntries_)
        if (e.name == "Init")
            return loadPreset(e);
    return juce::Result::fail("Init preset not found");
}

juce::Result PresetManager::step(int direction)
{
    ensureSynced();
    const auto list = getOrderedPresets();
    if (list.empty())
        return juce::Result::fail("No presets");

    const int n = static_cast<int>(list.size());
    int index = -1;
    for (int i = 0; i < n; ++i)
        if (list[static_cast<size_t>(i)].isSameAs(current_))
        {
            index = i;
            break;
        }

    juce::Result last = juce::Result::ok();
    for (int attempt = 0; attempt < n; ++attempt)
    {
        index = index < 0 ? (direction > 0 ? 0 : n - 1) : ((index + direction) % n + n) % n;
        last = loadPreset(list[static_cast<size_t>(index)]);
        if (last.wasOk())
            return last;
    }
    return last;
}

juce::Result PresetManager::loadNext()
{
    return step(1);
}
juce::Result PresetManager::loadPrevious()
{
    return step(-1);
}

// ---- Files
// -----------------------------------------------------------------------------------------------------

juce::File PresetManager::fileFor(const juce::String& category, const juce::String& name) const
{
    return userFolder_.getChildFile(category).getChildFile(name + kFileExtension);
}

bool PresetManager::userPresetExists(const juce::String& name, const juce::String& category) const
{
    juce::String cat;
    const auto n = sanitiseName(name);
    return n.isNotEmpty() && resolveCategory(category, cat).wasOk() && fileFor(cat, n).existsAsFile();
}

juce::String PresetManager::uniqueName(const juce::String& base, const juce::String& category) const
{
    auto candidate = base;
    for (int k = 2; fileFor(category, candidate).existsAsFile(); ++k)
        candidate = base + " " + juce::String(k);
    return candidate;
}

juce::Result PresetManager::resolveCategory(const juce::String& category, juce::String& out)
{
    if (category.trim().isEmpty())
    {
        out = kUnsortedCategory;
        return juce::Result::ok();
    }
    out = sanitiseName(category);
    if (out.isEmpty())
        return juce::Result::fail("Invalid category name");
    return juce::Result::ok();
}

juce::Result PresetManager::writePresetFile(const juce::File& dest, const PresetEntry& meta,
                                            const ParamValues& values) const
{
    juce::XmlElement root(kPresetTag);
    root.setAttribute("stateVersion", kStateVersion);
    root.setAttribute("name", meta.name);
    root.setAttribute("category", meta.category);
    root.setAttribute("author", meta.author);
    auto* state = root.createNewChildElement(kStateTag);
    for (const auto& [id, value] : values)
    {
        auto* p = state->createNewChildElement("PARAM");
        p->setAttribute("id", id);
        p->setAttribute("value", static_cast<double>(value));
    }

    if (!dest.getParentDirectory().createDirectory())
        return juce::Result::fail("Cannot create folder " + dest.getParentDirectory().getFullPathName());

    juce::TemporaryFile tmp(dest);
    if (!root.writeTo(tmp.getFile()) || !tmp.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Cannot write " + dest.getFullPathName());
    return juce::Result::ok();
}

// ---- Operations
// ------------------------------------------------------------------------------------------------

juce::Result PresetManager::save()
{
    ensureSynced();
    if (current_.factory)
        return juce::Result::fail("Factory presets are read-only. Use Save As.");
    const auto file =
        current_.file != juce::File() ? current_.file : fileFor(current_.category, current_.name);
    PresetEntry meta = current_;
    if (meta.author.isEmpty())
        meta.author = author_;
    const auto values = captureValues();
    if (auto r = writePresetFile(file, meta, values); r.failed())
        return r;
    meta.file = file;
    rescan();
    setCurrent(meta, values);
    notify();
    return juce::Result::ok();
}

juce::Result PresetManager::saveAs(const juce::String& name, const juce::String& category, bool overwrite)
{
    ensureSynced();
    const auto n = sanitiseName(name);
    if (n.isEmpty())
        return juce::Result::fail("Invalid preset name");
    juce::String cat;
    if (auto r = resolveCategory(category, cat); r.failed())
        return r;

    const auto dest = fileFor(cat, n);
    const bool isCurrentFile =
        !current_.factory && current_.file != juce::File() && samePath(current_.file, dest);
    if (dest.existsAsFile() && !overwrite && !isCurrentFile)
        return juce::Result::fail("A preset named \"" + n + "\" already exists in " + cat);

    PresetEntry meta;
    meta.name = n;
    meta.category = cat;
    meta.author = author_.isNotEmpty() ? author_ : (current_.factory ? juce::String() : current_.author);
    meta.factory = false;
    meta.file = dest;

    const auto values = captureValues();
    if (auto r = writePresetFile(dest, meta, values); r.failed())
        return r;
    rescan();
    setCurrent(meta, values);
    notify();
    return juce::Result::ok();
}

juce::Result PresetManager::rename(const juce::String& newName)
{
    ensureSynced();
    if (current_.factory)
        return juce::Result::fail("Factory presets are read-only");
    const auto n = sanitiseName(newName);
    if (n.isEmpty())
        return juce::Result::fail("Invalid preset name");
    if (n == current_.name)
        return juce::Result::ok();

    const auto oldFile =
        current_.file != juce::File() ? current_.file : fileFor(current_.category, current_.name);
    PresetEntry meta;
    ParamValues values;
    if (auto r = parsePresetFile(oldFile, meta, values); r.failed())
        return juce::Result::fail("Preset file is missing or damaged: " + r.getErrorMessage());

    const auto dest = oldFile.getParentDirectory().getChildFile(n + kFileExtension);
    if (dest.existsAsFile() && !samePath(dest, oldFile))
        return juce::Result::fail("A preset named \"" + n + "\" already exists in " + current_.category);

    meta.name = n;
    if (auto r = writePresetFile(dest, meta, values); r.failed())
        return r;
    if (!samePath(dest, oldFile))
        oldFile.deleteFile();

    auto updated = current_;
    updated.name = n;
    updated.file = dest;
    rescan();
    setCurrent(updated, hasBaseline_ ? baseline_ : captureValues()); // keep the modified state
    notify();
    return juce::Result::ok();
}

juce::Result PresetManager::deleteCurrent()
{
    ensureSynced();
    if (current_.factory)
        return juce::Result::fail("Factory presets cannot be deleted");
    const auto file =
        current_.file != juce::File() ? current_.file : fileFor(current_.category, current_.name);
    if (file.existsAsFile() && !file.deleteFile())
        return juce::Result::fail("Cannot delete " + file.getFullPathName());

    const auto folder = file.getParentDirectory();
    if (folder != userFolder_ && folder.getParentDirectory() == userFolder_ &&
        folder.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0)
        folder.deleteFile();

    rescan();
    return loadInit();
}

juce::Result PresetManager::duplicateCurrent()
{
    ensureSynced();
    const auto cat = current_.factory ? juce::String(kUnsortedCategory) : current_.category;
    const auto name = uniqueName(sanitiseName(current_.name + " copy"), cat);

    PresetEntry meta;
    meta.name = name;
    meta.category = cat;
    meta.author = author_;
    meta.file = fileFor(cat, name);
    const auto values = captureValues();
    if (auto r = writePresetFile(meta.file, meta, values); r.failed())
        return r;
    rescan();
    setCurrent(meta, values);
    notify();
    return juce::Result::ok();
}

juce::Result PresetManager::importPreset(const juce::File& file, PresetEntry* created)
{
    PresetEntry meta;
    ParamValues values;
    if (auto r = parsePresetFile(file, meta, values); r.failed())
        return r;

    auto name = sanitiseName(meta.name);
    if (name.isEmpty())
        name = sanitiseName(file.getFileNameWithoutExtension());
    if (name.isEmpty())
        name = "Imported preset";

    juce::String cat;
    if (resolveCategory(meta.category.isEmpty() ? juce::String("Imported") : meta.category, cat).failed())
        cat = "Imported";

    PresetEntry entry = meta;
    entry.name = uniqueName(name, cat); // never overwrite an existing preset
    entry.category = cat;
    entry.factory = false;
    entry.file = fileFor(cat, entry.name);
    if (auto r = writePresetFile(entry.file, entry, resolveValues(values)); r.failed())
        return r;
    rescan();
    if (created != nullptr)
        *created = entry;
    notify();
    return juce::Result::ok();
}

juce::Result PresetManager::exportCurrent(const juce::File& destination)
{
    ensureSynced();
    auto dest = destination;
    if (!dest.hasFileExtension(kFileExtension))
        dest = dest.withFileExtension(kFileExtension);
    PresetEntry meta = current_;
    if (meta.author.isEmpty())
        meta.author = author_;
    return writePresetFile(dest, meta, captureValues());
}

// ---- A/B
// -------------------------------------------------------------------------------------------------------

void PresetManager::selectSlot(Slot slot)
{
    ensureSynced();
    if (slot == activeSlot_)
        return;

    auto& from = slots_[activeSlot_ == Slot::A ? 0u : 1u];
    from.valid = true;
    from.values = captureValues();
    from.ref = current_;
    from.hasBaseline = hasBaseline_;
    from.baseline = baseline_;

    auto& to = slots_[slot == Slot::A ? 0u : 1u];
    if (!to.valid)
        to = from; // first switch: both slots start identical

    activeSlot_ = slot;
    applyValues(to.values, &to.ref);
    current_ = to.ref;
    hasBaseline_ = to.hasBaseline;
    baseline_ = to.baseline;
    needsSync_ = false;
    lastModified_ = computeModified();
    notify();
}
} // namespace pentagrit
