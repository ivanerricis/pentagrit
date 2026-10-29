// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/PresetBar.h"

namespace pentagrit
{
namespace
{
constexpr int kArrowWidth = 30;
constexpr int kNameWidth = 168;
constexpr int kSlotWidth = 26;
constexpr int kGap = 2;
constexpr int kGroupGap = 8;

void addEntryItems(juce::PopupMenu& menu, const std::vector<PresetEntry>& entries, const PresetEntry& current,
                   const std::function<void(const PresetEntry&)>& pick)
{
    for (const auto& e : entries)
    {
        juce::PopupMenu::Item item(e.name);
        item.setTicked(e.isSameAs(current));
        item.setAction([pick, e] { pick(e); });
        menu.addItem(std::move(item));
    }
}
} // namespace

PresetBar::PresetBar(PresetManager& manager) : manager_(manager)
{
    for (auto* b : {&prev_, &name_, &next_, &menu_, &slotA_, &slotB_})
        addAndMakeVisible(*b);

    prev_.setTooltip("Previous preset");
    next_.setTooltip("Next preset");
    name_.setTooltip("Preset browser (Factory / User)");
    menu_.setTooltip("Preset menu: Save, Save As, Rename, Delete, Duplicate, Import, Export");
    slotA_.setTooltip("Compare: state A");
    slotB_.setTooltip("Compare: state B");

    slotA_.setClickingTogglesState(false);
    slotB_.setClickingTogglesState(false);
    slotA_.setConnectedEdges(juce::Button::ConnectedOnRight);
    slotB_.setConnectedEdges(juce::Button::ConnectedOnLeft);

    prev_.onClick = [this] { report(manager_.loadPrevious()); };
    next_.onClick = [this] { report(manager_.loadNext()); };
    name_.onClick = [this] { showBrowserMenu(); };
    menu_.onClick = [this] { showActionsMenu(); };
    slotA_.onClick = [this] { manager_.selectSlot(PresetManager::Slot::A); };
    slotB_.onClick = [this] { manager_.selectSlot(PresetManager::Slot::B); };

    manager_.addChangeListener(this);
    refresh();
}

PresetBar::~PresetBar()
{
    manager_.removeChangeListener(this);
}

int PresetBar::getPreferredWidth() const
{
    return kArrowWidth * 3 + kNameWidth + kGap * 3 + kGroupGap + kSlotWidth * 2;
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    const int fixed = kArrowWidth * 3 + kGap * 3 + kGroupGap + kSlotWidth * 2;
    const int nameWidth = juce::jmax(40, r.getWidth() - fixed);

    prev_.setBounds(r.removeFromLeft(kArrowWidth));
    r.removeFromLeft(kGap);
    name_.setBounds(r.removeFromLeft(nameWidth));
    r.removeFromLeft(kGap);
    next_.setBounds(r.removeFromLeft(kArrowWidth));
    r.removeFromLeft(kGap);
    menu_.setBounds(r.removeFromLeft(kArrowWidth));
    r.removeFromLeft(kGroupGap);
    slotA_.setBounds(r.removeFromLeft(kSlotWidth));
    slotB_.setBounds(r.removeFromLeft(kSlotWidth));
}

void PresetBar::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void PresetBar::refresh()
{
    name_.setButtonText(manager_.getDisplayName());
    const bool a = manager_.getActiveSlot() == PresetManager::Slot::A;
    slotA_.setToggleState(a, juce::dontSendNotification);
    slotB_.setToggleState(!a, juce::dontSendNotification);
    repaint();
}

void PresetBar::report(const juce::Result& result, const juce::String& successMessage)
{
    if (result.wasOk())
    {
        if (successMessage.isNotEmpty() && onMessage)
            onMessage(successMessage);
        return;
    }
    if (onMessage)
        onMessage(result.getErrorMessage());
    else
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Pentagrit presets",
                                               result.getErrorMessage());
}

// ---- Browser
// ---------------------------------------------------------------------------------------------------

void PresetBar::showBrowserMenu()
{
    manager_.rescan(); // pick up files added or removed outside of the plugin
    const auto current = manager_.getCurrentEntry();
    juce::Component::SafePointer<PresetBar> safe(this);
    auto pick = [safe](const PresetEntry& e)
    {
        if (safe != nullptr)
            safe->report(safe->manager_.loadPreset(e));
    };

    juce::PopupMenu root;

    juce::PopupMenu factory;
    for (const auto& cat : manager_.getFactoryCategories())
    {
        juce::PopupMenu sub;
        addEntryItems(sub, manager_.getPresetsInCategory(true, cat), current, pick);
        factory.addSubMenu(cat, sub, true, nullptr, current.factory && current.category == cat);
    }
    root.addSubMenu("Factory", factory, true, nullptr, current.factory);

    juce::PopupMenu user;
    bool anyUser = false;
    for (const auto& cat : manager_.getUserCategories())
    {
        const auto entries = manager_.getPresetsInCategory(false, cat);
        if (entries.empty())
            continue;
        anyUser = true;
        juce::PopupMenu sub;
        addEntryItems(sub, entries, current, pick);
        user.addSubMenu(cat, sub, true, nullptr, !current.factory && current.category == cat);
    }
    if (!anyUser)
        user.addItem("(empty)", false, false, nullptr);
    root.addSubMenu("User", user, true, nullptr, !current.factory);

    root.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&name_).withMinimumWidth(name_.getWidth()));
}

// ---- Actions menu
// ----------------------------------------------------------------------------------------------

void PresetBar::showActionsMenu()
{
    const bool factory = manager_.isCurrentFactory();
    juce::Component::SafePointer<PresetBar> safe(this);
    juce::PopupMenu m;

    m.addItem("Save",
              [safe]
              {
                  if (safe == nullptr)
                      return;
                  if (safe->manager_.isCurrentFactory())
                  {
                      if (safe->onMessage)
                          safe->onMessage("Factory presets are read-only. Use Save As.");
                      safe->promptSaveAs();
                  }
                  else
                      safe->report(safe->manager_.save(),
                                   "Saved \"" + safe->manager_.getCurrentName() + "\"");
              });
    m.addItem("Save As...",
              [safe]
              {
                  if (safe != nullptr)
                      safe->promptSaveAs();
              });
    m.addItem("Rename...", !factory, false,
              [safe]
              {
                  if (safe != nullptr)
                      safe->promptRename();
              });
    m.addItem("Delete...", !factory, false,
              [safe]
              {
                  if (safe != nullptr)
                      safe->confirmDelete();
              });
    m.addItem("Duplicate",
              [safe]
              {
                  if (safe != nullptr)
                      safe->report(safe->manager_.duplicateCurrent(),
                                   "Duplicated as \"" + safe->manager_.getCurrentName() + "\"");
              });
    m.addSeparator();
    m.addItem("Import...",
              [safe]
              {
                  if (safe != nullptr)
                      safe->chooseImport();
              });
    m.addItem("Export...",
              [safe]
              {
                  if (safe != nullptr)
                      safe->chooseExport();
              });

    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menu_));
}

void PresetBar::promptSaveAs()
{
    auto* aw = new juce::AlertWindow("Save preset as", "Choose a name and a category.",
                                     juce::MessageBoxIconType::NoIcon, this);
    aw->addTextEditor("name", manager_.getCurrentName(), "Name");
    juce::StringArray cats = manager_.getUserCategories();
    cats.addIfNotAlreadyThere(PresetManager::kUnsortedCategory);
    aw->addComboBox("category", cats, "Category (pick or type a new one)");
    auto* combo = aw->getComboBoxComponent("category");
    combo->setEditableText(true);
    combo->setText(manager_.isCurrentFactory() ? juce::String(PresetManager::kUnsortedCategory)
                                               : manager_.getCurrentCategory(),
                   juce::dontSendNotification);
    aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PresetBar> safe(this);
    aw->enterModalState(true,
                        juce::ModalCallbackFunction::create(
                            [safe, aw](int result)
                            {
                                if (result != 1 || safe == nullptr)
                                    return;
                                const auto name = aw->getTextEditorContents("name");
                                const auto cat = aw->getComboBoxComponent("category")->getText();
                                auto r = safe->manager_.saveAs(name, cat, false);
                                if (r.failed() && safe->manager_.userPresetExists(name, cat))
                                {
                                    juce::AlertWindow::showAsync(
                                        juce::MessageBoxOptions::makeOptionsOkCancel(
                                            juce::MessageBoxIconType::QuestionIcon, "Overwrite preset?",
                                            "A preset with this name already exists. Overwrite it?",
                                            "Overwrite", "Cancel", safe.getComponent()),
                                        [safe, name, cat](int answer)
                                        {
                                            if (answer == 1 && safe != nullptr)
                                                safe->report(safe->manager_.saveAs(name, cat, true), "Saved");
                                        });
                                    return;
                                }
                                safe->report(r, "Saved \"" + safe->manager_.getCurrentName() + "\"");
                            }),
                        true);
}

void PresetBar::promptRename()
{
    auto* aw = new juce::AlertWindow("Rename preset", {}, juce::MessageBoxIconType::NoIcon, this);
    aw->addTextEditor("name", manager_.getCurrentName(), "Name");
    aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PresetBar> safe(this);
    aw->enterModalState(true,
                        juce::ModalCallbackFunction::create(
                            [safe, aw](int result)
                            {
                                if (result == 1 && safe != nullptr)
                                    safe->report(safe->manager_.rename(aw->getTextEditorContents("name")),
                                                 "Renamed");
                            }),
                        true);
}

void PresetBar::confirmDelete()
{
    juce::Component::SafePointer<PresetBar> safe(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                                     "Delete \"" + manager_.getCurrentName() + "\"?",
                                                     "Delete", "Cancel", this),
        [safe](int r)
        {
            if (r == 1 && safe != nullptr)
                safe->report(safe->manager_.deleteCurrent(), "Deleted");
        });
}

void PresetBar::chooseImport()
{
    chooser_ = std::make_unique<juce::FileChooser>("Import preset", juce::File(),
                                                   juce::String("*") + PresetManager::kFileExtension);
    juce::Component::SafePointer<PresetBar> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe](const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (safe == nullptr || file == juce::File())
                                  return;
                              PresetEntry created;
                              auto r = safe->manager_.importPreset(file, &created);
                              if (r.wasOk())
                                  r = safe->manager_.loadPreset(created);
                              safe->report(r, "Imported \"" + created.name + "\"");
                          });
}

void PresetBar::chooseExport()
{
    const auto initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                             .getChildFile(PresetManager::sanitiseName(manager_.getCurrentName()) +
                                           PresetManager::kFileExtension);
    chooser_ = std::make_unique<juce::FileChooser>("Export preset", initial,
                                                   juce::String("*") + PresetManager::kFileExtension);
    juce::Component::SafePointer<PresetBar> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe](const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (safe == nullptr || file == juce::File())
                                  return;
                              safe->report(safe->manager_.exportCurrent(file),
                                           "Exported to " + file.getFileName());
                          });
}
} // namespace pentagrit
